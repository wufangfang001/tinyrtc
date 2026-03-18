# TinyRTC

A lightweight, WebRTC-compatible implementation written in pure GNU99 C, designed for portability across Linux and RTOS systems.

> 🛠️ This project is **100% developed with [OpenClaw](https://github.com/openclaw/openclaw)** - an AI coding agent for collaborative software development.
> 
> We encourage other developers to continue developing this project using OpenClaw and welcome pull requests!

## Overview

TinyRTC aims to provide a minimal, fully compatible WebRTC stack that can run on resource-constrained systems while maintaining interoperability with standard WebRTC implementations (like browsers).

## Features

- [ ] Signal processing
- [ ] P2P connection (ICE/STUN/TURN)
- [ ] Audio/video RTP/RTCP transmission
- [ ] Congestion control (CC) evaluation
- [ ] Weak network resistance optimization
- [ ] Pure GNU99 C implementation
- [ ] Platform abstraction via AOSL
- [ ] Compatible with standard WebRTC

## Dependencies

- [AOSL](https://github.com/AgoraIO-Community/aosl) - Agora Open Source Library, provides platform abstraction for memory, threads, logging and network operations.
- [mbed TLS](https://github.com/ARMmbed/mbedtls) - Lightweight crypto library for DTLS handshake and SRTP encryption. Included as git submodule.

## API Design

TinyRTC follows WebRTC conventions with a minimal C API:

```c
// 1. Initialize SDK
tinyrtc_config_t config;
tinyrtc_get_default_config(&config);
tinyrtc_context_t *ctx = tinyrtc_init(&config);

// 2. Create PeerConnection
tinyrtc_pc_config_t pc_config = {0};
pc_config.stun_server = "stun:stun.l.google.com:19302";
pc_config.observer.on_connection_state_change = my_callback;
tinyrtc_peer_connection_t *pc = tinyrtc_peer_connection_create(ctx, &pc_config);

// 3. Add media tracks (application provides encoded RTP packets)
tinyrtc_track_config_t video = {
    .kind = TINYRTC_TRACK_KIND_VIDEO,
    .mid = "v0",
    .payload_type = 100,
    .codec_name = "H264",
    .clock_rate = 90000
};
tinyrtc_track_t *track = tinyrtc_peer_connection_add_track(pc, &video);

// 4. Create offer/answer and exchange via your signaling channel
char *offer = NULL;
tinyrtc_peer_connection_create_offer(pc, &offer);
// send offer to remote...

// 5. In main loop, process events
while (connected) {
    tinyrtc_process_events(ctx, 100);
    // Send encoded RTP packets when you have them
    tinyrtc_track_send_rtp(track, rtp_data, rtp_len);
}

// 6. Cleanup
tinyrtc_peer_connection_destroy(pc);
tinyrtc_destroy(ctx);
```

**Key Design Points:**
- TinyRTC **does not include** codec implementations. Applications provide encoded RTP packets for sending, and receive encoded RTP packets for decoding.
- TinyRTC **does not handle** signaling transport. Applications exchange SDP/ICE candidates via their own signaling channel.
- All platform dependencies are abstracted via AOSL, making porting to new platforms easy.

## Testing with Browser

The `tools/` directory contains `browser_test.html` - a simple browser-based test page that supports **two modes**:

### Mode 1: Automatic Signaling (Recommended)

Uses public signaling server to automatically connect:

1. Build TinyRTC first (see Building section)
2. On TinyRTC side, run receiver demo with a room ID:
   ```bash
   ./build/demo/tinyrtc_recv --room my-test-room
   ```
   Or run sender demo:
   ```bash
   ./build/demo/tinyrtc_send --room my-test-room
   ```
3. Open `tools/browser_test.html` in a modern browser
4. Click "Start Camera & Microphone"
5. In "Automatic Signaling" section, enter the **same room ID** and click "Start Automatic Signaling"
6. SDP will be exchanged automatically via public signaling server
7. Connection established - you can see video from browser

The public signaling server `wss://signal-master.appspot.com:443` is used by default. However, this public server is often unreachable. We provide a simple Python signaling server for local area network testing:

### Run your own signaling server (Recommended for LAN testing)

We provide two versions of simple signaling server (same functionality):

- `simple-signaling-server.py` - uses the `websockets` library (cleaner code)
- `simple-signaling-server-raw.py` - **no third-party dependencies**, pure asyncio TCP with manual WebSocket handshake

Start the server:

```bash
# Start simple signaling server on port 8080 (needs websockets library)
cd tools/
python3 simple-signaling-server.py 8080

# Or use the raw version (no dependencies)
python3 simple-signaling-server-raw.py 8080
```

Then use it with:
```bash
# In TinyRTC receiver
./tinyrtc_recv --room my-test-room --server ws://your-server-ip:8080

# In browser_test.html, the signaling URL will be automatically used
# when you connect with the same room ID
```

You can specify your own signaling server with command line:
```bash
./tinyrtc_recv --room my-test-room --server ws://your-signaling-server:8080
```

### Mode 2: Manual SDP Exchange (Original)

1. Open `tools/browser_test.html` in a modern browser
2. Click "Start Camera & Microphone"
3. Create offer in browser, copy to `offer.sdp`
4. On TinyRTC side: run `./tinyrtc_recv --offer offer.sdp` to get `answer.sdp`
5. Paste answer back to browser
6. Connection established, you can see video from browser

## Building

```bash
# Clone the main project first
# We use the archive/mbedtls-2.28 branch (LTS maintenance branch) which is compatible.
# mbedtls 3.x has incompatible API changes, we currently only support 2.28.
git clone  https://github.com/wufangfang001/tinyrtc.git
cd tinyrtc/
git submodule update --init --remote

# ⚠️ Note: --depth=1 and --shallow-submodules is not recommended - it may not correctly
# check out the specified branch for mbedtls and causes build errors.
# Full clone is recommended for correct submodule branch tracking.

# Build for Linux
mkdir build && cd build
cmake ..
make -j$(nproc)

# Build for RTOS
# Use your platform build system - just compile the .c files and link with AOSL
```

## Architecture

See [ARCHITECTURE.md](./ARCHITECTURE.md) for detailed architecture documentation.

## Development Tasks

See [TASKS.md](./TASKS.md) for current development progress.

## License

MIT License - see [LICENSE](./LICENSE) for details
