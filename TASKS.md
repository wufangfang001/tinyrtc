# TinyRTC Development Tasks

This file tracks the step-by-step development tasks for TinyRTC.

## Phase 1: Project Infrastructure ✅

- [x] Create project directory structure
- [x] Add AOSL as git submodule
- [x] Create basic CMake build system
- [x] Write architecture documentation
- [x] Write API header templates

## Phase 2: Core Infrastructure ✅

- [x] Initialize AOSL integration
- [x] Implement basic types and macros (`src/include/common.h`)
- [x] Implement logging via AOSL
- [x] Implement memory management (wrappers around AOSL)
- [x] Implement error handling (macros)
- [ ] Add unit test infrastructure

## Phase 3: SDP / Signal Module ✅

- [x] SDP parser implementation (`src/signal/sdp_parser.c`)
- [x] SDP generator (`src/signal/sdp_generator.c`)
- [x] Offer/answer negotiation structures
- [x] Session description management
- [x] Signaling callback interface

## Phase 4: Network / ICE Module ✅

- [x] Socket abstraction wrapper on AOSL
- [x] STUN client implementation (`src/ice/stun.c`)
- [x] ICE agent (`src/ice/ice.c`)
- [x] Candidate representation and parsing
- [x] Candidate gathering
- [x] Connectivity checks
- [x] ICE nomination
- [x] TURN client implementation (optional) - placeholder, can be added later

## Phase 5: DTLS / SRTP ✅

- [x] DTLS handshake implementation (`src/media/dtls.c`)
- [x] Certificate generation (self-signed ECDSA P-256)
- [x] Key extraction (SRTP key derivation per RFC 5764)
- [x] SRTP initialization (`src/media/srtp.c`)
- [x] SRTP encryption/decryption (AES-GCM)

## Phase 6: RTP / RTCP ✅

- [x] RTP header parsing/building (`src/media/rtp.c`)
- [x] RTCP packet processing (SR/RR parsing/building)
- [x] Jitter buffer implementation (adaptive delay)
- [x] Sequence number tracking (handled by jitter buffer ordering)
- [ ] NACK/PLI support - can be added later with RTCP feedback

## Phase 7: Media Module ✅

- [x] Media codec interface (`src/media_codec.c`)
- [x] RTP packetization/depacketization (`src/media/rtp.c`)
- [x] Packetization for large frames into MTU chunks
- [x] Track management (local/remote with jitter buffer)
- [x] Payload type negotiation (via SDP)
- [ ] Media I/O interface - integrated via ICE

## Phase 8: Congestion Control ✅

- [x] Bandwidth estimation (exponential moving average)
- [x] AIMD rate adaptation (Additive Increase / Multiplicative Decrease)
- [x] Receiver-side feedback via RTCP RR
- [x] Packet pacing based on current target bitrate
- [x] Weak network resistance (dynamic bitrate adjustment)
- [x] Full implementation: `src/cc/congestion_control.c`

## Phase 9: Peer Connection ✅

- [x] Peer connection state machine (`src/peer_connection.c`)
- [x] Integration of all modules
- [x] Public API implementation
- [x] Congestion control integration
- [ ] Call flow testing

## Phase 10: Demo & Testing ✅

- [x] Linux demo: sender application (`demo/demo_sender.c`)
- [x] Linux demo: receiver application (`demo/demo_receiver.c`)
- [ ] Test with browser WebRTC
- [ ] Interoperability testing
- [ ] Performance benchmarking
- [ ] Memory leak detection

## Future Improvements

- [ ] SCTP data channel support
- [ ] BBR congestion control (alternative algorithm)
- [ ] Additional codec support
- [ ] RTSP integration
- [ ] SIMULCAST support
- [ ] NACK/PLI RTCP feedback
