# TinyRTC Architecture

## Overview

TinyRTC follows a modular layered architecture design, with clear separation between protocol layers and platform abstraction. All platform-dependent operations go through the AOSL interface to ensure portability.

## Layered Structure

```
┌─────────────────────────────────────────────────────────────┐
│  Application Layer (user provided)                          │
├─────────────────────────────────────────────────────────────┤
│  Public API (include/tinyrtc/*.h)                           │
├─────────────────────────────────────────────────────────────┤
│  Signal Module  │  ICE Module  │  Media Module  │  CC      │
├─────────────────────────────────────────────────────────────┤
│  RTP/RTCP Stack │  SCTP Data   │  DTLS/SRTP     │  Network │
├─────────────────────────────────────────────────────────────┤
│  AOSL Platform Abstraction                                  │
├─────────────────────────────────────────────────────────────┤
│  OS: Linux / RTOS / Other                                    │
└─────────────────────────────────────────────────────────────┘
```

## Directory Structure

```
tinyrtc/
├── include/            # Public API (only exposed to applications)
│   └── tinyrtc/
│       ├── tinyrtc.h         # Main SDK include file - users only need this
│       ├── peer_connection.h  # PeerConnection API
│       └── signal.h          # SDP parsing/generation
├── src/               # Core implementation
│   ├── include/        # Internal header files (private to implementation)
│   │   ├── media.h    # RTP/RTCP internal
│   │   └── network.h # Network internal
│   ├── signal/        # SDP and signaling
│   ├── ice/           # ICE/STUN/TURN
│   ├── media/         # RTP/RTCP/DTLS/SRTP
│   ├── net/           # Network
│   ├── cc/            # Congestion control
│   └── tinyrtc.c      # Core SDK
├── demo/              # Example applications
├── tools/             # Testing utilities (browser test page)
├── platform/          # Platform-specific code (if needed)
├── third_party/
│   └── aosl/         # AOSL platform abstraction (git submodule)
```

## Module Descriptions

### 1. Public API

Only these public headers are exposed to applications:
- `tinyrtc - `include/tinyrtc/tinyrtc.h - Core SDK initialization
- `peer_connection.h - `include/tinyrtc/peer_connection.h - Peer connection management
- `signal.h - `include/tinyrtc/ - SDP parsing and generation (for application signaling

All implementation-internal headers are in `src/include/` and are not exposed externally.

### 2. Core Modules

#### Signal Module (`src/signal/`)
- Handles SDP (Session Description Protocol) parsing/generation
- Application handles signaling transport (TinyRTC doesn't implement signaling)
- Offer/answer negotiation

#### ICE Module (`src/ice/`)
- STUN client implementation
- TURN client support
- Candidate gathering
- Connectivity check
- P2P P2P connection establishment

#### Media Module (`src/media/`)
- Audio/video track management
- RTP packetization/depacketization
- RTCP processing
- SRTP encryption/decryption
- DTLS key negotiation
- **Note: TinyRTC does NOT include codec implementations - codecs are handled by application/platform.**

#### Congestion Control Module (`src/cc/`)
- Bandwidth estimation
- Congestion detection
- Rate adaptation
- Weak network resistance algorithms

#### Network Module (`src/net/`)
- Socket abstraction via AOSL
- Packet scheduling
- Port allocation

### 2. Core Modules

#### Signal Module (`src/signal/`)
- Handles SDP (Session Description Protocol) parsing/generation
- Signaling transport abstraction
- Offer/answer negotiation

#### ICE Module (`src/ice/`)
- STUN client implementation
- TURN client support
- Candidate gathering
- Connectivity check
- P2P connection establishment

#### Media Module (`src/media/`)
- Audio/video track management
- RTP packetization/depacketization
- RTCP processing
- SRTP encryption/decryption
- DTLS key negotiation

#### Congestion Control Module (`src/cc/`)
- Bandwidth estimation
- Congestion detection
- Rate adaptation
- Weak network resistance algorithms

#### Network Module (`src/net/`)
- Socket abstraction via AOSL
- Packet scheduling
- Port allocation

### 3. Protocol Stack

#### RTP/RTCP
- RFC 3550 compliant implementation
- Header parsing/construction
- Sequence number tracking
- Jitter buffer management

#### DTLS/SRTP
- DTLS 1.2 handshake for key exchange
- SRTP encryption/authentication
- Certificate generation/management

#### SCTP (optional)
- For data channel support
- Stream multiplexing

## Platform Abstraction via AOSL

All system operations are delegated to AOSL:

| Functionality         | AOSL Interface Used        |
|-----------------------|----------------------------|
| Memory allocation     | `aosl_malloc`, `aosl_free` |
| Threading             | `aosl_thread_create`, etc. |
| Mutex/Synchronization | `aosl_mutex_t`             |
| Network sockets       | `aosl_socket_t`            |
| Logging               | `aosl_log`                 |
| Time                  | `aosl_gettime`             |
| Random                | `aosl_random`              |

This design allows TinyRTC to be ported to any platform that has AOSL support.

## Threading Model

- **Single thread main loop** (default) - All processing runs in a single thread, application calls `tinyrtc_process()` periodically
- **Optional multi-thread** - Network I/O can be offloaded to separate threads via AOSL threading
- **Thread safe** - Public API is protected by mutex when compiled with thread support

## Memory Management

- All memory allocations go through AOSL
- No static global buffers (reentrant)
- Configurable maximum memory usage
- Pool-based allocation for frequent small allocations (optional)

## Data Flow

```
1. Application creates PeerConnection
   ↓
2. Generate offer / set remote description
   ↓
3. ICE candidate gathering starts
   ↓
4. Candidates exchanged via signaling (app-mediated)
   ↓
5. Connectivity check establishes connection
   ↓
6. DTLS handshake completes
   ↓
7. SRTP keys derived
   ↓
8. Media streams start flowing
   ↓
9. RTP packets sent/received → CC adjusts bandwidth
```

## Compilation Options

CMake build options:

- `TINYRTC_BUILD_EXAMPLES` - Build demo applications
- `TINYRTC_BUILD_TESTS` - Build unit tests
- `TINYRTC_ENABLE_DEBUG` - Enable debug logging
- `TINYRTC_ENABLE_SCTP` - Enable SCTP for data channels
- `TINYRTC_ENABLE_TURN` - Enable TURN support
- `TINYRTC_MAX_PEERS` - Maximum number of concurrent peer connections
- `TINYRTC_MAX_CANDIDATES` - Maximum candidates per peer
