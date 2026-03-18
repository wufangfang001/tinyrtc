#!/usr/bin/env python3
"""
TinyRTC Simple Signaling Server - raw TCP version
- 直接使用asyncio TCP，不使用websockets库，完全手工处理WebSocket握手和帧解析
- 这样完全兼容我们的协议，因为客户端已经完成了握手
"""

import asyncio
import json
import base64
import hashlib
import sys

# Connected clients: {room_id: [(writer, client_id), ...]}
rooms = {}

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

async def handle_client(reader, writer):
    print("New client connected")
    
    # Read HTTP handshake request
    request = await reader.read(4096)
    request = request.decode('utf-8')
    # print(f"Request:\n{request}")
    
    # Find Sec-WebSocket-Key
    key = None
    room_id = None
    for line in request.split('\r\n'):
        if line.startswith('Sec-WebSocket-Key: '):
            key = line.split(': ', 1)[1]
    
    # Generate accept key
    accept = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
    
    # Send handshake response
    response = (
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Accept: {accept}\r\n"
        "\r\n"
    )
    writer.write(response.encode())
    await writer.drain()
    
    print("WebSocket handshake completed")
    
    current_room = None
    client_id = None
    
    try:
        while True:
            # Read WebSocket frame
            first_byte = await reader.read(1)
            if not first_byte:
                break
            
            b1 = first_byte[0]
            fin = (b1 & 0x80) != 0
            opcode = b1 & 0x0F
            
            second_byte = await reader.read(1)
            if not second_byte:
                break
            
            b2 = second_byte[0]
            mask = (b2 & 0x80) != 0
            payload_len = b2 & 0x7F
            
            # Extended length
            if payload_len == 126:
                ext_len = await reader.read(2)
                payload_len = int.from_bytes(ext_len, byteorder='big')
            elif payload_len == 127:
                ext_len = await reader.read(8)
                payload_len = int.from_bytes(ext_len, byteorder='big')
            
            # Client to server must be masked
            if mask:
                mask_key = await reader.read(4)
            else:
                mask_key = None
            
            # Read payload
            payload = await reader.read(payload_len)
            if len(payload) < payload_len:
                print(f"Incomplete payload: got {len(payload)} expected {payload_len}")
                break
            
            # Unmask
            if mask:
                payload = bytes([payload[i] ^ mask_key[i % 4] for i in range(len(payload))])
            
            payload_text = payload.decode('utf-8')
            # print(f"Received: {payload_text[:200]}")
            
            # Parse JSON
            try:
                data = json.loads(payload_text)
            except json.JSONDecodeError as e:
                print(f"JSON decode error: {e}")
                continue
            
            # Handle presence check
            if "checkPresence" in data and "channel" in data:
                room_id = data["channel"]
                current_room = room_id
                print(f"Presence check for room: {room_id}")
                
                # Add client to room
                if room_id not in rooms:
                    rooms[room_id] = []
                # Find client id from message
                client_id = "tinyrtc-" + str(id(writer))
                rooms[room_id].append((writer, client_id))
                print(f"Client added to room {room_id}, total {len(rooms[room_id])} clients")
                continue
            
            # Normal message
            if "channel" in data and "sender" in data:
                room_id = data["channel"]
                sender_id = data["sender"]
                current_room = room_id
                
                # Add to room if not there
                if room_id not in rooms:
                    rooms[room_id] = []
                found = False
                for w, cid in rooms[room_id]:
                    if w == writer:
                        found = True
                        break
                if not found:
                    client_id = sender_id
                    rooms[room_id].append((writer, client_id))
                    print(f"Client added to room {room_id}")
                
                # Broadcast to other clients in this room
                if room_id in rooms:
                    count = 0
                    for other_writer, other_client_id in rooms[room_id]:
                        if other_writer != writer:
                            # Just forward the entire message
                            # We need to encapsulate it in a WebSocket text frame
                            payload_bytes = payload_text.encode()
                            pl_len = len(payload_bytes)
                            
                            # Build frame header
                            header = bytearray()
                            header.append(0x81)  # FIN + TEXT
                            
                            if pl_len <= 125:
                                header.append(pl_len)
                            elif pl_len <= 0xFFFF:
                                header.append(126)
                                header.extend(pl_len.to_bytes(2, byteorder='big'))
                            else:
                                header.append(127)
                                header.extend(pl_len.to_bytes(8, byteorder='big'))
                            
                            # Server -> client is not masked (per RFC6455)
                            try:
                                other_writer.write(header)
                                other_writer.write(payload_bytes)
                                await other_writer.drain()
                                count += 1
                            except Exception as e:
                                print(f"Failed to send to client: {e}")
                    print(f"Forwarded message to {count} clients in room {room_id}")
                    
    except Exception as e:
        print(f"Exception: {e}")
    finally:
        # Clean up
        if current_room and current_room in rooms:
            rooms[current_room] = [(w, cid) for w, cid in rooms[current_room] if w != writer]
            if len(rooms[current_room]) == 0:
                del rooms[current_room]
        print("Client disconnected")
        writer.close()
        await writer.wait_closed()

async def main():
    port = 8080
    if len(sys.argv) > 1:
        port = int(sys.argv[1])
    
    print(f"Starting TinyRTC simple signaling server on port {port} (raw TCP)")
    print()
    print("Testing instructions:")
    print("  1. Start this server: python3 simple-signaling-server-raw.py 8080")
    print("  2. On server machine IP:  run")
    print(f"     ./tinyrtc_send --room test-123 --server ws://your-server-ip:{port}")
    print("  3. In browser open tools/browser_test.html, change signaling URL to:")
    print(f"     ws://your-server-ip:{port} and enter same room id")
    print("  4. Enjoy!")
    print()
    
    server = await asyncio.start_server(handle_client, '0.0.0.0', port)
    
    addrs = ', '.join(str(sock.getsockname()) for sock in server.sockets)
    print(f"Serving on {addrs}")
    
    async with server:
        await server.serve_forever()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nServer stopped")
