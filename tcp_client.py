import socket
import sys
import time

# 创建一个socket:
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
# 建立连接:
s.connect(('127.0.0.1', 2222))
print("connect success")
s.send("hello xv6!!!".encode('utf-8'))
s.send("this message came from python!!!".encode('utf-8'))
print(s.recv(1024))

# time.sleep(2)
s.close()

# s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
# data = "this is a ping!".encode('utf-8')
# s.sendto(data, ('127.0.0.1', 1234))