try:
    import network
    import espnow,time
    import json,binascii
    import machine 
    import ubinascii
except ImportError:
    print("ImportError")
    raise SystemExit

class esp_now(object):
    def __init__(self):
        self.sta=network.WLAN(network.STA_IF)
        self.sta.active(True)
        self.sta.disconnect()
        self.e=espnow.ESPNow()
        self.e.active(True)
        self.host_mac=self.sta.config('mac')
        self.peer='ffffffffffff'.upper() 
        self.timeout_ms = 5000
        self.default_pmk = b"mpython"
        self.sync = True
    
    def get_mac(self):
        mac = ubinascii.hexlify(machine.unique_id()).decode().upper()
        return mac
        
    def send_msg(self,data): #发送消息，这里只会发给实例化以后注册的最后一个设备地址
        try:
            self.e.send(self.peer, data)
        except OSError as err:
            if len(err.args) < 2:
                raise err
            if err.args[1] == 'ESP_ERR_ESPNOW_NOT_INIT':
                self.e.active(True)
            elif err.args[1] == 'ESP_ERR_ESPNOW_NOT_FOUND':
                self.e.add_peer(self.peer)
            elif err.args[1] == 'ESP_ERR_ESPNOW_IF':
                self.sta = network.WLAN(network.STA_IF).active(True)
            else:
                raise err
        
    def add_peer(self,addr='ffffffffffff',channel=0): #注册设备
        self.peer = self.hex_str_to_bytes(addr)
        self.e.add_peer(self.peer,channel=channel) 
        
    def hex_to_bytes(self,b_data):
        # 字节码传输JSON封装有出错可能所以可以字节化一下
        hex_key=binascii.hexlify(b_data).decode('ascii')
        return hex_key
    
    def bytes_to_hex(self,b_data):
        b_hex=binascii.unhexlify(b_data)
        return b_hex

    def hex_str_to_bytes(self,hex_str):
        # str 表示的十六进制字符串到 bytes 的转换
        return bytes.fromhex(hex_str)
    
    def bytes_to_hex_str(self,b):
        # bytes 到 十六进制字符串（str）的转换
        hex_str = b.hex() 
        return hex_str 
    
    def bytes_dictjson_bytes(self,key,value):# 这个是将字典里边键和值包装成一个字典字节化发出去
        key=self.hex_to_bytes(key)
        value=self.hex_to_bytes(value)
        b_data=json.dumps({key:value}).encode('utf-8')
        return b_data
    
    def bytes_dictjson_dict(self,b_data):#解码恢复成字典键值
        b_dict=json.loads(b_data.decode('utf-8'))
        key= binascii.unhexlify(list(b_dict.keys())[0])
        value=binascii.unhexlify(b_dict[list(b_dict.keys())[0]])
        return key,value
    
    def recv_msg(self): 
        if self.e.any()>0:
            host_adr, recv_msg = self.e.recv()
            try:
                for peer, info in self.e.peers_table.items():
                    mac_address = ':'.join('{:02x}'.format(b) for b in peer)
                    rssi = info[0]
                    time_ms = info[1]
                    #print(f"MAC: {mac_address}, RSSI: {rssi} dBm, Time: {time_ms} ms")
                    #print(rssi)
                return host_adr.hex(),recv_msg.decode('utf-8'),rssi
            except: #8266没有检测信号的功能，所以这里如果出错那就是8266设备为了兼容写个出错例外
                return host_adr,recv_msg,0
            
    def recv_dict_msg(self): #接收数据啥都接收，具体业务要二次开发
        if self.e.any()>0:
            host_adr, recv_msg = self.e.recv()
            try:
                host_adr,recv_msg=self.bytes_dictjson_dict(recv_msg)
            except:
                pass
            try:
                for peer, info in self.e.peers_table.items():
                    mac_address = ':'.join('{:02x}'.format(b) for b in peer)
                    rssi = info[0]
                    time_ms = info[1]
                    #print(f"MAC: {mac_address}, RSSI: {rssi} dBm, Time: {time_ms} ms")
                    #print(rssi)
                return host_adr.hex(),recv_msg.decode('utf-8'),rssi
            except: #8266没有检测信号的功能，所以这里如果出错那就是8266设备为了兼容写个出错例外
                return host_adr,recv_msg,0
    
    def recv_cb(self):
        while True:  # Read out all messages waiting in the buffer
            mac, msg = self.e.irecv(0)  # Don't wait if no messages left
            if mac is None:
                return
            # print(mac, msg)

    def irq(self,fun):
        self.e.irq(fun(self.e))

            