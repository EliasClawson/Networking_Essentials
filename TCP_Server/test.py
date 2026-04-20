import socket

client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
client.connect(("localhost", 8083))

client.send(b"\x40\x00\x00\x0bHello World")

print(client.recv(1024))
