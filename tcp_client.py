import socket
import sys

# 创建一个socket:
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
# 建立连接:
s.connect(('127.0.0.1', 12345))

# s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
# data = "this is a ping!".encode('utf-8')
# s.sendto(data, ('127.0.0.1', 12345))