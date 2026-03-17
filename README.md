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

The `tools/` directory contains `browser_test.html` - a simple browser-based test page:

1. Open `tools/browser_test.html` in a modern browser
2. Click "Start Camera & Microphone"
3. Create offer in browser, copy to `offer.sdp`
4. On TinyRTC side: run `./tinyrtc_recv --offer offer.sdp` to get `answer.sdp`
5. Paste answer back to browser
6. Connection established, you can see video from browser

## Building

```bash
# Clone with all submodules (important, must add --recursive)
# We use the archive/mbedtls-2.28 branch (LTS maintenance branch) which is compatible.
# mbedtls 3.x has incompatible API changes, we currently only support 2.28.
#
# .gitmodules already specifies the branch, so git clone will automatically checkout correct branch:
git clone --recursive https://github.com/wufangfang001/tinyrtc.git
cd tinyrtc

# (Optional for shallow clone, faster download (~15MB instead of 300MB+):
# git clone --recursive --depth=1 --shallow-submodules https://github.com/wufangfang001/tinyrtc

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
