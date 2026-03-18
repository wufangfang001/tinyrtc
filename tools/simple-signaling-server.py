#!/usr/bin/env python3
"""
TinyRTC Simple Signaling Server
- 简单的WebSocket信令服务器，用于局域网内测试
- 协议兼容Muaz Khan/WebRTC-Experiment格式
- 支持房间，消息广播给房间内其他客户端

Usage:
  python3 simple-signaling-server.py [port]
  Default port: 8080

Then in TinyRTC demo:
  ./tinyrtc_send --room test-123
  (uses ws://your-ip:8080 as signaling server)

Or with wss if you have SSL certificates:
  (modify code to enable SSL)
"""

import asyncio
import websockets
import json
import sys

# Connected clients: {room_id: [client1, client2, ...]}
rooms = {}

async def handle_client(websocket):
    client_room = None
    
    try:
        async for message in websocket:
            try:
                data = json.loads(message)
                print(f"Received: {json.dumps(data, indent=2)}")
                
                # Check presence check format
                if "checkPresence" in data and "channel" in data:
                    # Response - we don't actually need to respond to presence
                    # Just continue, client will send offer/answer
                    room_id = data["channel"]
                    if room_id not in rooms:
                        rooms[room_id] = []
                    print(f"Presence check for room: {room_id}")
                    continue
                
                # Normal message format according to protocol
                if "channel" in data and "sender" in data:
                    room_id = data["channel"]
                    client_room = room_id
                    
                    # Add client to room if not already
                    if room_id not in rooms:
                        rooms[room_id] = []
                    if websocket not in rooms[room_id]:
                        rooms[room_id].append(websocket)
                    
                    # Broadcast to other clients in the same room
                    if len(rooms[room_id]) > 1:
                        # Message is already JSON encoded string
                        # We just forward it
                        for client in rooms[room_id]:
                            if client != websocket:
                                try:
                                    await client.send(message)
                                    print(f"Forwarded message to other client in room {room_id}")
                                except Exception as e:
                                    print(f"Failed to forward: {e}")
                                    
            except json.JSONDecodeError as e:
                print(f"JSON decode error: {e}")
                continue
            except Exception as e:
                print(f"Error processing message: {e}")
                continue
                
    except websockets.exceptions.ConnectionClosed:
        print("Client disconnected")
    finally:
        # Remove client from room
        if client_room and client_room in rooms:
            if websocket in rooms[client_room]:
                rooms[client_room].remove(websocket)
            if len(rooms[client_room]) == 0:
                del rooms[client_room]

async def main():
    port = 8080
    if len(sys.argv) > 1:
        port = int(sys.argv[1])
    
    print(f"Starting TinyRTC simple signaling server on port {port}")
    print(f"Usage:")
    print(f"  1. Start this server on your server machine")
    print(f"  2. In browser: open browser_test.html and change signaling URL to ws://your-server-ip:{port}")
    print(f"  3. Run ./tinyrtc_send --room test-123 after changing default server in demo")
    print()
    
    async with websockets.serve(handle_client, "0.0.0.0", port):
        await asyncio.Future()  # run forever

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nServer stopped")
