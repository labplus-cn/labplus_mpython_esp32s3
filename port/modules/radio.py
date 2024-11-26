import network
import espnow,time
import json,binascii

class esp_now(object):
    def __init__(self):
        self.sta=network.WLAN(network.STA_IF)
        self.sta.active(True)
        self.sta.disconnect()
        self.e=espnow.ESPNow()
        self.e.active(True)
        self.host_mac=self.sta.config('mac')
        
    def send_msg (self,data): #发送消息，这里只会发给实例化以后注册的最后一个设备地址
        self.e.send(self.peer, data)
        
    def send_dict_msg(self,key,value):
        data=self.bytes_dictjson_bytes(key,value)
        self.e.send(self.peer, data)
        return data
    
    def recv_dict_msg (self): #接收数据啥都接收，具体业务要二次开发
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
                return host_adr,recv_msg,rssi
            except: #8266没有检测信号的功能，所以这里如果出错那就是8266设备为了兼容写个出错例外
                return host_adr,recv_msg,0

    def add_peer(self,addr): #注册设备，最后一个是有效的
        self.peer = addr
        self.e.add_peer(addr) 
        
    def hex_to_bytes(self,b_data):#因为字节码传输JSON封装有出错可能所以可以字节化一下，只是备用放这里
        hex_key=binascii.hexlify(b_data).decode('ascii')
        return hex_key
    
    def bytes_to_hex(self,b_data):
        b_hex=binascii.unhexlify(b_data)
        return b_hex
    
    def bytes_dictjson_bytes(self,key,value):#这个是将字典里边建和值包装成一个字典字节化发出去
        key=self.hex_to_bytes(key)
        value=self.hex_to_bytes(value)
        b_data=json.dumps({key:value}).encode('utf-8')
        return  b_data
    
    def bytes_dictjson_dict(self,b_data):#解码恢复成字典键值
        b_dict=json.loads(b_data.decode('utf-8'))
        key= binascii.unhexlify(list(b_dict.keys())[0])
        value=binascii.unhexlify(b_dict[list(b_dict.keys())[0]])
        return key,value
    
    def recv_msg (self): #接收数据啥都接收，具体业务要二次开发
        if self.e.any()>0:
            host_adr, recv_msg = self.e.recv()
            try:
                for peer, info in self.e.peers_table.items():
                    mac_address = ':'.join('{:02x}'.format(b) for b in peer)
                    rssi = info[0]
                    time_ms = info[1]
                    #print(f"MAC: {mac_address}, RSSI: {rssi} dBm, Time: {time_ms} ms")
                    #print(rssi)
                return host_adr,recv_msg,rssi
            except: #8266没有检测信号的功能，所以这里如果出错那就是8266设备为了兼容写个出错例外
                return host_adr,recv_msg,0
            