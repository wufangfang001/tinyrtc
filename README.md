# TinyRTC

A lightweight, WebRTC-compatible implementation written in pure GNU99 C, designed for portability across Linux and RTOS systems.

> 🛠️ This project is **100% developed with [OpenClaw](https://github.com/openclaw/openclaw)** - an AI coding agent for collaborative software development.
> 
> We encourage other developers to continue developing this project using OpenClaw and welcome pull requests!

## Overview

TinyRTC aims to provide a minimal, fully compatible WebRTC stack that can run on resource-constrained systems while maintaining interoperability with standard WebRTC implementations (like browsers).

## Features

- [x] P2P connection (ICE/STUN)
- [x] DTLS/SRTP key exchange and encryption
- [x] Audio/video RTP/RTCP transmission
- [x] Jitter buffer (adaptive delay)
- [x] Congestion control (AIMD) with bandwidth estimation
- [x] SDP parsing and generation
- [x] Pure GNU99 C implementation
- [x] Platform abstraction via AOSL
- [ ] TURN client (placeholder, can be added later)
- [ ] NACK/PLI RTCP feedback
- [ ] SCTP data channel support
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

## Browser Interoperability Testing

### Mode 1: Automatic Signaling (Recommended)

Uses the third-party `sdp-transfer` signaling server to automatically connect:

1. Build TinyRTC first (see Building section)
2. On TinyRTC side, run receiver demo with a room ID:
   ```bash
   ./build/demo/tinyrtc_recv --room my-test-room --server ws://your-server-ip:8765
   ```
   Or run sender demo:
   ```bash
   ./build/demo/tinyrtc_send --room my-test-room --server ws://your-server-ip:8765
   ```
3. Open the browser demo provided by your `sdp-transfer` deployment
4. Click "Start Camera & Microphone"
5. Enter the **same room ID** and make sure the signaling URL points to your `sdp-transfer` instance
6. SDP will be exchanged automatically via `sdp-transfer`
7. Connection established - you can see video from browser

Current TinyRTC development should assume the signaling backend is the external `sdp-transfer` project. Historical references to `wss://signal-master.appspot.com:443` and `wss://wsnodejs.nodejitsu.com:443` are obsolete.

Current limitation: the `sdp-transfer` browser demo sends `RTCSessionDescription` / `RTCIceCandidate` JSON objects, while TinyRTC's signaling parser still expects SDP as a plain string and does not fully parse remote ICE candidate objects yet. For reliable debugging, treat the stock `sdp-transfer` browser demo as a signaling reference, not as guaranteed end-to-end interoperability proof.

### Run `sdp-transfer` (Recommended for LAN testing)

`sdp-transfer` is maintained separately from TinyRTC. Follow that project's README to install and start the service. The current TinyRTC integration assumes these endpoints by default:

- **WS signaling**: `ws://your-server-ip:8765`
- **WSS signaling**: `wss://your-server-ip:8766`

Then use it with:
```bash
# TinyRTC receiver over WS
./tinyrtc_recv --room my-test-room --server ws://your-server-ip:8765

# TinyRTC sender over WS
./tinyrtc_send --room my-test-room --server ws://your-server-ip:8765

# TinyRTC sender over WSS with self-signed certificate
./tinyrtc_send --room my-test-room --server wss://your-server-ip:8766 --no-verify
```

For self-signed WSS deployments:

1. Open `https://your-server-ip:8766` once in the browser and trust the certificate
2. Use `wss://your-server-ip:8766` in the `sdp-transfer` browser demo
3. Add `--no-verify` to TinyRTC demo commands

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

## Running Unit Tests

TinyRTC uses minunit (lightweight single-header C unit testing framework). To build and run tests:

```bash
mkdir -p build && cd build
cmake .. -DTINYRTC_BUILD_TESTS=ON
make -j$(nproc)
./tests/tinyrtc_tests
```

All tests **must pass** before any pull request can be merged.

## Architecture

See [ARCHITECTURE.md](./ARCHITECTURE.md) for detailed architecture documentation.

## Development Tasks

See [TASKS.md](./TASKS.md) for current development progress.

## Project Documentation

- [ARCHITECTURE.md](./ARCHITECTURE.md) - Overall architecture documentation
- [docs/memory/ARCHITECTURE_NOTES.md](./docs/memory/ARCHITECTURE_NOTES.md) - Architecture decision records
- [docs/memory/API_CHANGES.md](./docs/memory/API_CHANGES.md) - API change history
- [docs/memory/BUGS.md](./docs/memory/BUGS.md) - Known bugs and limitations
- [docs/memory/DEVELOPMENT_RULES.md](./docs/memory/DEVELOPMENT_RULES.md) - Development workflow rules

## License

MIT License - see [LICENSE](./LICENSE) for details
