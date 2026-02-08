# Python Socket Programming

## Overview

Python Socket Programming enables network communication between applications using the TCP/IP protocol stack. Python's `socket` module provides a low-level networking interface, while higher-level libraries like `asyncio` offer modern asynchronous patterns for building scalable network applications.

---

## 1. The Socket Module

The `socket` module is Python's core interface for network programming, providing access to the BSD socket interface.

### Basic Concepts

- **Socket**: An endpoint for sending/receiving data
- **Address Family**: AF_INET (IPv4), AF_INET6 (IPv6), AF_UNIX (local)
- **Socket Types**: SOCK_STREAM (TCP), SOCK_DGRAM (UDP)

### TCP Client Example

```python
import socket

def tcp_client(host='localhost', port=8080):
    """Simple TCP client"""
    # Create a TCP socket
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    
    try:
        # Connect to server
        client_socket.connect((host, port))
        print(f"Connected to {host}:{port}")
        
        # Send data
        message = "Hello, Server!"
        client_socket.sendall(message.encode('utf-8'))
        
        # Receive response
        response = client_socket.recv(1024)
        print(f"Received: {response.decode('utf-8')}")
        
    except socket.error as e:
        print(f"Socket error: {e}")
    finally:
        client_socket.close()

if __name__ == "__main__":
    tcp_client()
```

### TCP Server Example

```python
import socket
import threading

def handle_client(client_socket, address):
    """Handle individual client connection"""
    print(f"Connection from {address}")
    
    try:
        # Receive data
        data = client_socket.recv(1024)
        if data:
            print(f"Received: {data.decode('utf-8')}")
            
            # Send response
            response = f"Echo: {data.decode('utf-8')}"
            client_socket.sendall(response.encode('utf-8'))
    except socket.error as e:
        print(f"Error handling client: {e}")
    finally:
        client_socket.close()

def tcp_server(host='0.0.0.0', port=8080):
    """Multi-threaded TCP server"""
    # Create socket
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    
    # Set socket options
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    # Bind to address
    server_socket.bind((host, port))
    
    # Listen for connections (backlog of 5)
    server_socket.listen(5)
    print(f"Server listening on {host}:{port}")
    
    try:
        while True:
            # Accept connection
            client_socket, address = server_socket.accept()
            
            # Handle in separate thread
            client_thread = threading.Thread(
                target=handle_client,
                args=(client_socket, address)
            )
            client_thread.start()
    except KeyboardInterrupt:
        print("\nShutting down server...")
    finally:
        server_socket.close()

if __name__ == "__main__":
    tcp_server()
```

### UDP Socket Example

```python
import socket

def udp_server(host='0.0.0.0', port=9090):
    """UDP server example"""
    # Create UDP socket
    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_socket.bind((host, port))
    
    print(f"UDP server listening on {host}:{port}")
    
    try:
        while True:
            # Receive data (no connection needed)
            data, address = udp_socket.recvfrom(1024)
            print(f"Received from {address}: {data.decode('utf-8')}")
            
            # Send response
            response = f"Echo: {data.decode('utf-8')}"
            udp_socket.sendto(response.encode('utf-8'), address)
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        udp_socket.close()

def udp_client(host='localhost', port=9090):
    """UDP client example"""
    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    try:
        message = "Hello, UDP Server!"
        udp_socket.sendto(message.encode('utf-8'), (host, port))
        
        # Set timeout
        udp_socket.settimeout(5)
        
        data, server = udp_socket.recvfrom(1024)
        print(f"Received: {data.decode('utf-8')}")
    except socket.timeout:
        print("Request timed out")
    finally:
        udp_socket.close()
```

### Non-blocking Sockets with Select

```python
import socket
import select

def non_blocking_server(host='0.0.0.0', port=8080):
    """Non-blocking server using select()"""
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind((host, port))
    server_socket.listen(5)
    server_socket.setblocking(False)
    
    # Lists of sockets to monitor
    inputs = [server_socket]
    outputs = []
    message_queues = {}
    
    print(f"Non-blocking server on {host}:{port}")
    
    while inputs:
        # Wait for activity
        readable, writable, exceptional = select.select(
            inputs, outputs, inputs, 1.0
        )
        
        # Handle readable sockets
        for s in readable:
            if s is server_socket:
                # Accept new connection
                client_socket, address = s.accept()
                client_socket.setblocking(False)
                inputs.append(client_socket)
                message_queues[client_socket] = []
                print(f"New connection from {address}")
            else:
                # Receive data
                try:
                    data = s.recv(1024)
                    if data:
                        print(f"Received: {data.decode('utf-8')}")
                        message_queues[s].append(data)
                        if s not in outputs:
                            outputs.append(s)
                    else:
                        # Close connection
                        if s in outputs:
                            outputs.remove(s)
                        inputs.remove(s)
                        s.close()
                        del message_queues[s]
                except:
                    pass
        
        # Handle writable sockets
        for s in writable:
            try:
                if message_queues[s]:
                    message = message_queues[s].pop(0)
                    s.send(message)
                else:
                    outputs.remove(s)
            except:
                pass
        
        # Handle exceptional conditions
        for s in exceptional:
            inputs.remove(s)
            if s in outputs:
                outputs.remove(s)
            s.close()
            del message_queues[s]
```

---

## 2. Asyncio for Asynchronous Networking

`asyncio` provides a modern approach to concurrent network programming using coroutines.

### Asyncio TCP Echo Server

```python
import asyncio

async def handle_client(reader, writer):
    """Handle client connection asynchronously"""
    address = writer.get_extra_info('peername')
    print(f"Connection from {address}")
    
    try:
        while True:
            # Read data
            data = await reader.read(1024)
            if not data:
                break
            
            message = data.decode('utf-8')
            print(f"Received: {message}")
            
            # Echo back
            response = f"Echo: {message}"
            writer.write(response.encode('utf-8'))
            await writer.drain()
    except asyncio.CancelledError:
        pass
    finally:
        print(f"Closing connection from {address}")
        writer.close()
        await writer.wait_closed()

async def async_server(host='0.0.0.0', port=8080):
    """Asyncio TCP server"""
    server = await asyncio.start_server(
        handle_client, host, port
    )
    
    address = server.sockets[0].getsockname()
    print(f"Asyncio server on {address}")
    
    async with server:
        await server.serve_forever()

if __name__ == "__main__":
    asyncio.run(async_server())
```

### Asyncio TCP Client

```python
import asyncio

async def async_client(host='localhost', port=8080):
    """Asyncio TCP client"""
    try:
        # Open connection
        reader, writer = await asyncio.open_connection(host, port)
        
        # Send message
        message = "Hello from asyncio client!"
        writer.write(message.encode('utf-8'))
        await writer.drain()
        
        # Read response
        data = await reader.read(1024)
        print(f"Received: {data.decode('utf-8')}")
        
        # Close connection
        writer.close()
        await writer.wait_closed()
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    asyncio.run(async_client())
```

### Multiple Concurrent Connections

```python
import asyncio

async def fetch_data(host, port, message):
    """Fetch data from server"""
    try:
        reader, writer = await asyncio.open_connection(host, port)
        
        writer.write(message.encode('utf-8'))
        await writer.drain()
        
        data = await reader.read(1024)
        result = data.decode('utf-8')
        
        writer.close()
        await writer.wait_closed()
        
        return result
    except Exception as e:
        return f"Error: {e}"

async def multiple_clients():
    """Connect to multiple servers concurrently"""
    tasks = [
        fetch_data('localhost', 8080, 'Request 1'),
        fetch_data('localhost', 8080, 'Request 2'),
        fetch_data('localhost', 8080, 'Request 3'),
    ]
    
    # Run all tasks concurrently
    results = await asyncio.gather(*tasks, return_exceptions=True)
    
    for i, result in enumerate(results, 1):
        print(f"Response {i}: {result}")

if __name__ == "__main__":
    asyncio.run(multiple_clients())
```

### HTTP Client with Asyncio

```python
import asyncio

async def http_get(host, port, path):
    """Simple async HTTP GET request"""
    reader, writer = await asyncio.open_connection(host, port)
    
    # Send HTTP request
    request = f"GET {path} HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n"
    writer.write(request.encode('utf-8'))
    await writer.drain()
    
    # Read response
    response = await reader.read()
    
    writer.close()
    await writer.wait_closed()
    
    return response.decode('utf-8')

async def main():
    response = await http_get('example.com', 80, '/')
    print(response[:500])  # Print first 500 chars

if __name__ == "__main__":
    asyncio.run(main())
```

---

## 3. Python Networking Libraries

### Requests Library (HTTP)

```python
import requests

def http_examples():
    """HTTP requests using requests library"""
    
    # GET request
    response = requests.get('https://api.github.com')
    print(f"Status: {response.status_code}")
    print(f"JSON: {response.json()}")
    
    # POST request with JSON
    data = {'key': 'value'}
    response = requests.post(
        'https://httpbin.org/post',
        json=data
    )
    print(response.json())
    
    # Headers and authentication
    headers = {'User-Agent': 'MyApp/1.0'}
    response = requests.get(
        'https://api.github.com/user',
        headers=headers,
        auth=('username', 'password')
    )
    
    # Timeout and error handling
    try:
        response = requests.get(
            'https://example.com',
            timeout=5
        )
        response.raise_for_status()
    except requests.Timeout:
        print("Request timed out")
    except requests.HTTPError as e:
        print(f"HTTP error: {e}")
```

### aiohttp (Async HTTP)

```python
import aiohttp
import asyncio

async def aiohttp_examples():
    """Async HTTP with aiohttp"""
    
    async with aiohttp.ClientSession() as session:
        # GET request
        async with session.get('https://api.github.com') as response:
            data = await response.json()
            print(f"Status: {response.status}")
            print(f"Data: {data}")
        
        # POST request
        payload = {'key': 'value'}
        async with session.post(
            'https://httpbin.org/post',
            json=payload
        ) as response:
            result = await response.json()
            print(result)
        
        # Multiple concurrent requests
        urls = [
            'https://api.github.com',
            'https://httpbin.org/get',
            'https://example.com'
        ]
        
        tasks = [session.get(url) for url in urls]
        responses = await asyncio.gather(*tasks)
        
        for response in responses:
            print(f"{response.url}: {response.status}")
            response.close()

if __name__ == "__main__":
    asyncio.run(aiohttp_examples())
```

### Paramiko (SSH)

```python
import paramiko

def ssh_example():
    """SSH connection using paramiko"""
    
    # Create SSH client
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    
    try:
        # Connect
        ssh.connect(
            hostname='example.com',
            port=22,
            username='user',
            password='password'
        )
        
        # Execute command
        stdin, stdout, stderr = ssh.exec_command('ls -la')
        
        # Read output
        output = stdout.read().decode('utf-8')
        errors = stderr.read().decode('utf-8')
        
        print("Output:", output)
        if errors:
            print("Errors:", errors)
        
        # SFTP file transfer
        sftp = ssh.open_sftp()
        sftp.put('local_file.txt', '/remote/path/file.txt')
        sftp.get('/remote/path/file.txt', 'downloaded_file.txt')
        sftp.close()
        
    finally:
        ssh.close()
```

### WebSocket with websockets

```python
import asyncio
import websockets

# WebSocket Server
async def websocket_server(websocket, path):
    """WebSocket echo server"""
    async for message in websocket:
        print(f"Received: {message}")
        await websocket.send(f"Echo: {message}")

async def start_ws_server():
    async with websockets.serve(websocket_server, "localhost", 8765):
        print("WebSocket server started on ws://localhost:8765")
        await asyncio.Future()  # Run forever

# WebSocket Client
async def websocket_client():
    """WebSocket client"""
    uri = "ws://localhost:8765"
    async with websockets.connect(uri) as websocket:
        # Send message
        await websocket.send("Hello WebSocket!")
        
        # Receive response
        response = await websocket.recv()
        print(f"Received: {response}")
```

### Advanced Socket Programming Example

```python
import socket
import struct
import json

class SocketProtocol:
    """Custom protocol with length-prefixed messages"""
    
    @staticmethod
    def send_message(sock, message):
        """Send length-prefixed message"""
        # Encode message
        data = json.dumps(message).encode('utf-8')
        
        # Send length (4 bytes, network byte order)
        length = struct.pack('!I', len(data))
        sock.sendall(length + data)
    
    @staticmethod
    def recv_message(sock):
        """Receive length-prefixed message"""
        # Read length
        length_data = sock.recv(4)
        if not length_data:
            return None
        
        length = struct.unpack('!I', length_data)[0]
        
        # Read message
        data = b''
        while len(data) < length:
            chunk = sock.recv(length - len(data))
            if not chunk:
                raise ConnectionError("Connection closed")
            data += chunk
        
        return json.loads(data.decode('utf-8'))

# Usage example
def protocol_server():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('0.0.0.0', 9000))
    server.listen(1)
    
    print("Protocol server listening on port 9000")
    
    while True:
        client, addr = server.accept()
        print(f"Connection from {addr}")
        
        try:
            # Receive message
            message = SocketProtocol.recv_message(client)
            print(f"Received: {message}")
            
            # Send response
            response = {"status": "success", "echo": message}
            SocketProtocol.send_message(client, response)
        finally:
            client.close()

def protocol_client():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(('localhost', 9000))
    
    try:
        # Send message
        message = {"type": "greeting", "text": "Hello!"}
        SocketProtocol.send_message(sock, message)
        
        # Receive response
        response = SocketProtocol.recv_message(sock)
        print(f"Response: {response}")
    finally:
        sock.close()
```

---

## Key Concepts Summary

1. **Socket Module**: Low-level network interface
   - TCP (SOCK_STREAM) for reliable, connection-oriented communication
   - UDP (SOCK_DGRAM) for fast, connectionless communication
   - Blocking, non-blocking, and select-based I/O

2. **Asyncio**: Modern async/await pattern
   - Coroutine-based concurrency
   - Event loop for efficient I/O multiplexing
   - Better scalability than threads

3. **High-Level Libraries**:
   - **requests/aiohttp**: HTTP client operations
   - **websockets**: Real-time bidirectional communication
   - **paramiko**: SSH and SFTP operations

4. **Best Practices**:
   - Always close sockets (use context managers)
   - Set appropriate timeouts
   - Handle exceptions properly
   - Use asyncio for scalable servers
   - Implement custom protocols carefully

This covers the fundamentals and practical applications of Python socket programming!