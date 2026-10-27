import ujson as json
import utime as time
import urequests
import urandom

QUOTA_IMG   = 1   # 文生图
QUOTA_AUDIO = 4   # 文生音频

# MicroPython 纪元（2000-01-01）与 Unix 纪元（1970-01-01）的差值（秒）
_EPOCH_OFFSET = 946684800


class AiThorClient:
    """
    封装 AiThor 大模型平台的五类接口：
      - 文生文  ask_workflow("text",  content)
      - 文生图  ask_workflow("img",   content)
      - 文生音频 ask_workflow("audio", content, duration=30)
      - 查询次数 get_quota(quota_type)
      - 保存次数 _save_quota(quota_type)  [内部自动调用]

    config 字段说明（由平台 getToken 返回后写入）：
      token              平台鉴权 token（用于次数查询/保存）
      accessToken        文本 / 图像 Bearer Token
      accessToken_audio  音频 Bearer Token
      deviceId           设备 ID
      userId             用户 ID
      host               次数接口域名前缀
      llmHost            大模型接口域名前缀
    """

    SAFETY_CODES = {"80001", "80003", "80004", "80005", "80006", "80011", "80012"}
    _AGENT_TEXT_IMG = ("68259b245cf238cb3cf0bb08", "68259b245cf238cb3cf0bb09")
    _AGENT_AUDIO    = ("683ea3475cf2ce36d4862464", "683ea3475cf2ce36d4862465")
    _WF_TEXT  = ("680a1aa35cf29454cadb73eb",  "1915361123365916674")
    _WF_IMG   = ("680a1bbb5cf2945428ea63b7",  "1915362331067985921")
    _WF_AUDIO = ("68959e1d5cf269810f747018",  None)

    def __init__(self, config=None):
        self.cfg = {}
        if config:
            self.update_config(config)


    def update_config(self, new_cfg):
        """合并新配置，自动去除 host / llmHost 末尾斜杠。"""
        self.cfg.update(new_cfg)
        for key in ("host", "llmHost"):
            v = self.cfg.get(key)
            if v:
                self.cfg[key] = v.rstrip("/")


    def _trace_id(self):
        """生成 32 位随机 hex 链路追踪 ID。"""
        return "".join("%02x" % urandom.getrandbits(8) for _ in range(16))

    def _headers(self, mode="text"):
        """
        按模式构造请求头。
          mode="text" / "img" → 使用 accessToken，appid=aicxjycpx-zxwljxpt
          mode="audio"        → 使用 accessToken_audio，appid=aicxjycpx-zxwljxpt
        """
        is_audio = (mode == "audio")
        token    = self.cfg.get("accessToken_audio" if is_audio else "accessToken") or ""
        sdk_ver  = "2.4.0" if is_audio else "2.6.0"

        return {
            "Content-Type": "application/json",
            "Authorization": "Bearer " + token,
            "x-trace-id":   self._trace_id(),
            "x-device-id":  str(self.cfg.get("deviceId", "")),
            "x-user-id":    str(self.cfg.get("userId", "")),
            "x-platform":   "Web",
            "x-appid":      "aicxjycpx-zxwljxpt",
            "x-user-tag":   "zxwl-tools" if is_audio else "changyan-ai-web-user",
            "x-extra":      json.dumps({"AppVersion": "1.0.0", "SDKVersion": sdk_ver}),
        }

    def _post(self, url, payload, headers):
        """统一 POST，返回 (response, error_str)。"""
        try:
            body = json.dumps(payload).encode("utf-8")
            return urequests.post(url, data=body, headers=headers), None
        except Exception as e:
            return None, "请求异常: " + str(e)


    @staticmethod
    def _read_json(res):
        """兼容 MicroPython urequests 和标准 requests 的 JSON 读取。"""
        try:
            return res.json()
        except Exception as e:
            print("[_read_json] json() 失败:", e)
        try:
            chunks = []
            while True:
                chunk = res.raw.readline()
                if not chunk:
                    break
                chunks.append(chunk)
            raw = b"".join(chunks).decode("utf-8").strip()
            print("[_read_json] raw 内容:", raw)
            if raw:
                return json.loads(raw)
        except Exception as e:
            print("[_read_json] raw 读取失败:", e)
        try:
            raw = res.content
            if isinstance(raw, bytes):
                raw = raw.decode("utf-8")
            return json.loads(raw)
        except Exception:
            pass
        try:
            return json.loads(res.text)
        except Exception:
            pass
        return {}

    def get_quota(self, quota_type):
        """
        查询剩余次数。
        返回剩余次数(int)，失败返回 -1。
        """
        url     = self.cfg.get("host", "") + "/system/aihelp/get"
        payload = {"token": self.cfg.get("token", ""), "type": quota_type}
        try:
            res  = urequests.post(url, data=json.dumps(payload).encode("utf-8"), headers={"Content-Type": "application/json"})
            print("get_quota res: ", res)
            data = self._read_json(res)
            print("get_quota data: ", data)
            res.close()
            if data.get("code") == 0:
                return (data.get("data") or {}).get("limit", -1)
        except Exception as e:
            print("[quota/get] 异常:", e)
        return -1

    def _save_quota(self, quota_type):
        """
        记录一次调用消耗（内部调用，调用成功后自动触发）。
        成功返回 True，失败返回 False（不抛异常，不中断主流程）。
        """
        url     = self.cfg.get("host", "") + "/system/aihelp/save"
        payload = {"token": self.cfg.get("token", ""), "type": quota_type}
        try:
            res  = urequests.post(url, data=json.dumps(payload).encode("utf-8"), headers={"Content-Type": "application/json"})
            data = self._read_json(res)
            print("_save_quota data: ", data)
            res.close()
            if data.get("code") == 0:
                print("[quota/save] type={} 记录成功".format(quota_type))
                return True
            print("[quota/save] 记录失败:", data.get("msg"))
        except Exception as e:
            print("[quota/save] 异常:", e)
        return False


    def _create_chat(self, mode):
        """
        创建对话，返回 chatId 字符串。失败返回 None。
        mode: "text" / "img" / "audio"
        """
        return self._trace_id()
        # url = self.cfg.get("llmHost", "") + \
        #       "/ai-thor-dispatcher/api/v1/chat-history/chat/create"

        # if mode == "audio":
        #     p_id, p_ver = self._AGENT_AUDIO
        #     name = "新建对话"
        # else:
        #     p_id, p_ver = self._AGENT_TEXT_IMG
        #     name = "新建图像生成对话" if mode == "img" else "新建对话"

        # payload = {
        #     "name":           name,
        #     "label":          "",
        #     "agentType":      "1",
        #     "productId":      p_id,
        #     "productVersion": p_ver,
        #     "createTime":     self.cfg.get("timestamp") or int((time.time() + _EPOCH_OFFSET) * 1000),
        #     "endTime":        0,
        # }
        # print("url: ", url)
        # print("payload: ", payload)
        # print("self._headers(mode): ", self._headers(mode))
        # res, err = self._post(url, payload, self._headers(mode))
        # if err:
        #     print("[chat/create] 网络异常:", err)
        #     return None
        # try:
        #     resp = res.json()
        #     res.close()
        #     if resp.get("code") == "0":
        #         return resp.get("data")
        #     print("[chat/create] 失败:", resp)
        # except Exception as e:
        #     print("[chat/create] 解析异常:", e)
        # return None


    @staticmethod
    def _extract_content(chunk):
        """
        从单帧 JSON 中按文档优先级提取 content 字符串：
          llmMessage.content → message.content → messageContent
        """
        llm_msg = chunk.get("llmMessage") or {}
        msg     = chunk.get("message") or {}
        return (
            llm_msg.get("content")
            or msg.get("content")
            or chunk.get("messageContent")
        )

    @staticmethod
    def _chunk_code(chunk):
        """从帧中读取业务状态码，统一转为字符串。"""
        status = chunk.get("statusBean") or {}
        raw    = chunk.get("code", status.get("code", "0"))
        return str(raw)

    def _iter_sse(self, res):
        """
        迭代 SSE 原始响应，逐行 yield 解析后的 JSON chunk。
        遇到 [DONE] 或空行自动停止。
        """
        while True:
            try:
                raw_line = res.raw.readline()
            except Exception as e:
                print("[sse] readline 异常:", e)
                break
            # print("[sse] raw_line:", raw_line)
            if not raw_line:
                break
            try:
                line = raw_line.decode("utf-8").strip()
            except Exception:
                continue
            if not line.startswith("data:"):
                continue
            body = line[5:].strip()
            if not body or body == "[DONE]":
                break
            try:
                yield json.loads(body)
            except Exception:
                continue


    def _parse_text(self, res):
        """
        文生文：拼接所有帧 output 字段，返回完整文本字符串。
        失败返回错误提示字符串。
        """
        full_text = ""
        for chunk in self._iter_sse(res):
            code = self._chunk_code(chunk)
            if code != "0":
                msg = chunk.get("msg") or (chunk.get("statusBean") or {}).get("message") or "生成失败"
                return "抱歉，小飞AI助手还需要再学习一下…（{}）".format(msg)

            raw = self._extract_content(chunk)
            if not raw:
                continue
            try:
                parsed = json.loads(raw)
                if isinstance(parsed, dict) and "output" in parsed:
                    full_text += str(parsed["output"])
            except Exception:
                if isinstance(raw, str):
                    full_text += raw

        return full_text if full_text else "抱歉，生成失败，请稍后再试。"

    def _parse_img(self, res):
        """
        文生图：从帧中提取图像 URL。
        成功返回 URL 字符串；失败返回错误提示字符串。
        """
        for chunk in self._iter_sse(res):
            code = self._chunk_code(chunk)
            if code != "0":
                if code in self.SAFETY_CODES:
                    return "抱歉，该内容无法生成图像，请尝试其他描述"
                msg = chunk.get("msg") or (chunk.get("statusBean") or {}).get("message") or ""
                return "图像生成失败{}".format("：" + msg if msg else "，请尝试其他描述")

            raw = self._extract_content(chunk)
            if not raw:
                continue

            if isinstance(raw, str) and raw.startswith("http"):
                return raw

            try:
                parsed = json.loads(raw)
                if isinstance(parsed, dict):
                    url = (parsed.get("imageUrl")
                           or parsed.get("url")
                           or parsed.get("output"))
                    if isinstance(url, str) and url.startswith("http"):
                        return url
            except Exception:
                if isinstance(raw, str) and raw.strip():
                    pass

        return "抱歉，图像生成失败，请稍后再试。"

    def _parse_audio(self, res):
        """
        文生音频：提取完整音频结果对象。
        成功返回 dict，包含：
          audioUrl (str), duration (float), lyrics (str|None), captions (str|None)
        失败返回错误提示字符串。
        """
        for chunk in self._iter_sse(res):
            code = self._chunk_code(chunk)
            if code != "0":
                if code in self.SAFETY_CODES:
                    return "抱歉，该内容无法生成音频，请尝试其他描述"
                msg = chunk.get("msg") or (chunk.get("statusBean") or {}).get("message") or ""
                return "音频生成失败{}".format("：" + msg if msg else "，请尝试其他描述")

            raw = self._extract_content(chunk)
            if not raw:
                continue

            if isinstance(raw, str) and raw.startswith("http"):
                return {"audioUrl": raw, "duration": None, "lyrics": None, "captions": None}

            try:
                parsed = json.loads(raw)
                if isinstance(parsed, dict) and parsed.get("audioUrl"):
                    return {
                        "audioUrl": parsed["audioUrl"],
                        "duration": parsed.get("duration"),
                        "lyrics":   parsed.get("lyrics"),
                        "captions": parsed.get("captions"),
                    }
            except Exception:
                pass

        return "抱歉，音频生成失败，请稍后再试。"

    def ask_workflow(self, mode, content, duration=30, chat_id=None):
        """
        大模型调用统一入口。

        参数：
          mode     : "text" / "img" / "audio"
          content  : 用户输入文本 / 提示词
          duration : 仅 audio 模式有效，音频时长（秒），默认 30
          chat_id  : 可选，复用已有对话 ID；不传则自动创建

        返回：
          "text"  → str  完整回复文本
          "img"   → str  图像 URL（成功）或错误提示（失败）
          "audio" → dict {audioUrl, duration, lyrics, captions}（成功）
                    或 str 错误提示（失败）

        注意：
          - 文生图 / 文生音频 调用成功后会自动调用 save 接口记录次数消耗。
          - 文生文 不涉及次数管理（文档未定义对应 type）。
        """
        if mode not in ("text", "img", "audio"):
            return "不支持的模式: " + mode

        remainCount = 0
        if mode == "img":
            remainCount = self.get_quota(QUOTA_IMG)
        elif mode == "audio":
            remainCount = self.get_quota(QUOTA_AUDIO)
        else:
            remainCount = 999
        if not remainCount or remainCount <= 0:
            return "大模型" + ("文生图" if mode == "img" else "文生音频") + "调用额度不足"
        print("remainCount: ", remainCount)

        if not chat_id:
            chat_id = self._create_chat(mode)
        if not chat_id:
            return "创建对话失败，请检查网络或 Token 是否有效"

        url = self.cfg.get("llmHost", "") + \
              "/ai-thor-dispatcher/api/v1/workflow/chat"

        if mode == "audio":
            agent_id, ver_id = self._AGENT_AUDIO
            wf_id, wf_ver    = self._WF_AUDIO
            custom_vars = {
                "appId":    "aicxjycpx-zxwljxpt",
                "prompt":   content,
                "duration": duration,
            }
        else:
            agent_id, ver_id = self._AGENT_TEXT_IMG
            if mode == "img":
                wf_id, wf_ver = self._WF_IMG
                custom_vars   = {"content": content}
            else:
                wf_id, wf_ver = self._WF_TEXT
                custom_vars   = {"content": content, "online": 1}

        shortcut = {"shortcutType": 0, "workflowId": wf_id}

        payload = {
            "debugMode":       False,
            "agentId":         agent_id,
            "agentType":       "1",
            "contentType":     1,
            "content":         content,
            "chatId":          chat_id,
            "customVariables": custom_vars,
            "shortcutData":    shortcut,
            "chatHistory":     [],
        }

        # print("ask_workflow url: ", url)
        # print("ask_workflow payload: ", payload)
        # print("ask_workflow json: ", json.dumps(payload))
        # print("ask_workflow self._headers(mode): ", self._headers(mode))

        res, err = self._post(url, payload, self._headers(mode))
        if err:
            print("ask_workflow err: ", err)
            return err

        try:
            if mode == "text":
                print("ask_workflow text res: ", res)
                result = self._parse_text(res)
            elif mode == "img":
                print("ask_workflow img  res: ", res)
                result = self._parse_img(res)
            else:
                print("ask_workflow audio res: ", res)
                result = self._parse_audio(res)
        except Exception as e:
            result = "解析响应异常: " + str(e)
        finally:
            try:
                res.close()
            except Exception:
                pass

        if mode == "img" and isinstance(result, str) and result.startswith("http"):
            self._save_quota(QUOTA_IMG)
        elif mode == "audio" and isinstance(result, dict) and result.get("audioUrl"):
            self._save_quota(QUOTA_AUDIO)

        print("ask_workflow: ", mode, result)
        return result


# ============================================================
#  调用示例
# ============================================================

# token_info = {
#     "token":              "PLATFORM_TOKEN",
#     "accessToken":        "LLM_ACCESS_TOKEN",
#     "accessToken_audio":  "LLM_AUDIO_TOKEN",
#     "deviceId":           "ESP32_DEV_001",
#     "userId":             "USER_123",
#     "host":               "https://test-psp-api.ceshiservice.cn",
#     "llmHost":            "https://ai-thor-prelt.ceshiservice.cn",
# }


# def main():
#     client = AiThorClient()
#     client.update_config(token_info)

#     print("\n[文生文]")
#     text_res = client.ask_workflow("text", "简单介绍下彩虹")
#     print(text_res)

#     print("\n[文生图]")
#     remaining = client.get_quota(QUOTA_IMG)
#     print("剩余次数:", remaining)
#     if remaining > 0:
#         img_res = client.ask_workflow("img", "赛博朋克风格的猫")
#         print("图像地址:", img_res)
#     else:
#         print("次数不足，跳过文生图")

#     print("\n[文生音频]")
#     remaining = client.get_quota(QUOTA_AUDIO)
#     print("剩余次数:", remaining)
#     if remaining > 0:
#         audio_res = client.ask_workflow("audio", "一首欢快的自然乐曲", duration=30)
#         if isinstance(audio_res, dict):
#             print("音频地址:", audio_res["audioUrl"])
#             print("时长(秒):", audio_res["duration"])
#             print("歌词:", audio_res["lyrics"])
#         else:
#             print("错误:", audio_res)
#     else:
#         print("次数不足，跳过文生音频")


# if __name__ == "__main__":
#     main()