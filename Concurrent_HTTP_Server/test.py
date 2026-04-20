import socket

# Set up the socket connection
client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
client.connect(("localhost", 8085))

# Formulate an HTTP GET request
http_request = b"GET /404.html HTTP/1.1\r\nHost: localhost\r\n\r\n"

# Send the HTTP request to the server
client.send(http_request)

totalResponse = b""
# Receive and print the server's response
while True:
    response = client.recv(1024)
    totalResponse += response
    if not response:
        break
print(totalResponse)
# Close the connection
client.close()
