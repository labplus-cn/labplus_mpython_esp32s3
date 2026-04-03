import xiaozhiAI as xiaozhi
import json
import time
from mpython import *
import audio

# 这是一个演示如何使用 xiaozhi 模块新增加的 MCP (Model Context Protocol) 功能的示例。
# MCP 允许 AI 服务器发现并调用设备端定义的“工具”。
# 本示例集成了 WiFi 连接、音频硬件初始化以及 MCP 回调处理。

# --- MCP 回调处理 ---
def handle_mcp_request(json_req_str):
    """
    处理来自 AI 服务器的 MCP 请求。
    """
    try:
        req = json.loads(json_req_str)
        method = req.get("method")
        req_id = req.get("id")
        params = req.get("params", {})
        
        print(f"\n[MCP Request] ID: {req_id}, Method: {method}")
        
        # 1. 初始化会话 (initialize)
        if method == "initialize":
            response = {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {
                        "tools": {}
                    },
                    "serverInfo": {
                        "name": "labplus-mpython-s3",
                        "version": "1.0.0"
                    }
                }
            }
            
        # 2. 列出设备支持的工具 (tools/list)
        elif method == "tools/list":
            response = {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "tools": [
                        {
                            "name": "get_sensor_data",
                            "description": "获取板载传感器数据（温度、光照等）",
                            "inputSchema": {
                                "type": "object",
                                "properties": {
                                    "type": {
                                        "type": "string",
                                        "enum": ["temp", "light", "all"],
                                        "description": "传感器类型"
                                    }
                                }
                            }
                        },
                        {
                            "name": "set_led_color",
                            "description": "设置板载 RGB 灯颜色",
                            "inputSchema": {
                                "type": "object",
                                "properties": {
                                    "r": {"type": "integer", "minimum": 0, "maximum": 255},
                                    "g": {"type": "integer", "minimum": 0, "maximum": 255},
                                    "b": {"type": "integer", "minimum": 0, "maximum": 255}
                                },
                                "required": ["r", "g", "b"]
                            }
                        }
                    ]
                }
            }
            
        # 3. 调用具体工具 (tools/call)
        elif method == "tools/call":
            tool_name = params.get("name")
            args = params.get("arguments", {})
            
            print(f" -> Calling Tool: {tool_name} with args: {args}")
            
            result_text = "Unknown error"
            if tool_name == "get_sensor_data":
                # 示例：获取光照强度和温度
                light_val = light.read()
                # 假设环境温度传感器使用内置或其他方式，这里模拟一个
                result_text = f"Light intensity: {light_val}lx, Temperature: 26.5°C"
            elif tool_name == "set_led_color":
                r, g, b = args.get("r", 0), args.get("g", 0), args.get("b", 0)
                # 设置所有板载 RGB 灯
                rgb.fill((r, g, b))
                rgb.write()
                result_text = f"Successfully set LED to RGB({r}, {g}, {b})"
            
            response = {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "content": [
                        {"type": "text", "text": result_text}
                    ],
                    "isError": False
                }
            }
            
        # 4. 未知方法
        else:
            response = {
                "jsonrpc": "2.0",
                "id": req_id,
                "error": {
                    "code": -32601,
                    "message": f"Method not found: {method}"
                }
            }
            
        # 发送响应回服务器
        print(f"[MCP Response] Sending response for ID: {req_id}")
        xiaozhi.mcp_send_response(json.dumps(response))
        
    except Exception as e:
        print(f"Error handling MCP: {e}")
        # 即使出错也尝试给个响应，避免底层 C 挂起
        error_id = req.get("id") if 'req' in locals() and req else None
        error_resp = {
            "jsonrpc": "2.0",
            "id": error_id,
            "error": {"code": -32603, "message": str(e)}
        }
        xiaozhi.mcp_send_response(json.dumps(error_resp))

# --- 其他语音会话回调 ---
def on_state(state):
    names = ["idle", "connecting", "connected", "listening", "speaking", "error"]
    print(f"[System State] {names[state]}")

def on_stt(text):
    print(f"[STT] You said: {text}")

# --- 主程序初始化 ---

# 1. 连接 WiFi
w = wifi()
w.connectWiFi("office", "wearemaker")

# 2. 初始化音频硬件 (麦克风 + 扬声器)
# audio.init()

# 3. 初始化 xiaozhi 模块并注册回调
# 注意：init() 会从 NVS 读取已激活的 url/token
try:
    xiaozhi.init(volume=80)
    xiaozhi.on_state(on_state)
    xiaozhi.on_stt(on_stt)
    xiaozhi.on_mcp(handle_mcp_request)
    
    # 4. 启动会话
    print("Starting Xiaozhi session...")
    xiaozhi.start()
    
    print("MCP Demo running. AI can now call your tools!")
    
    # 5. 主循环：持续派发回调
    while True:
        xiaozhi.poll()
        # 处理按键触发录音（可选）
        if button_a.value() == 0:
            xiaozhi.listen_start()
        elif button_b.value() == 0:
            xiaozhi.listen_stop()
        time.sleep_ms(20)

except Exception as e:
    print(f"Session Error: {e}")
    xiaozhi.stop()
    xiaozhi.deinit()
