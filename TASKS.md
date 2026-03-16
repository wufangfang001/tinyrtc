# TinyRTC Development Tasks

This file tracks the step-by-step development tasks for TinyRTC.

## Phase 1: Project Infrastructure ✅

- [x] Create project directory structure
- [x] Add AOSL as git submodule
- [x] Create basic CMake build system
- [x] Write architecture documentation
- [x] Write API header templates

## Phase 2: Core Infrastructure

- [ ] Initialize AOSL integration
- [ ] Implement basic types and macros
- [ ] Implement logging via AOSL
- [ ] Implement memory management
- [ ] Implement error handling
- [ ] Add unit test infrastructure

## Phase 3: SDP / Signal Module

- [ ] SDP parser implementation
- [ ] SDP generator
- [ ] Offer/answer negotiation
- [ ] Session description management
- [ ] Signaling callback interface

## Phase 4: Network / ICE Module

- [ ] Socket abstraction wrapper on AOSL
- [ ] STUN client implementation
- [ ] TURN client implementation (optional)
- [ ] Candidate representation and parsing
- [ ] Candidate gathering
- [ ] Connectivity checks
- [ ] ICE nomination

## Phase 5: DTLS / SRTP

- [ ] DTLS handshake implementation
- [ ] Certificate generation
- [ ] Key extraction
- [ ] SRTP initialization
- [ ] SRTP encryption/decryption

## Phase 6: RTP / RTCP

- [ ] RTP header parsing/building
- [ ] RTCP packet processing
- [ ] Jitter buffer implementation
- [ ] Sequence number tracking
- [ ] NACK/PLI support

## Phase 7: Media Module

- [ ] Track management
- [ ] Payload type negotiation
- [ ] Packetization for common codecs (H.264, OPUS)
- [ ] Depacketization
- [ ] Media I/O interface

## Phase 8: Congestion Control

- [ ] Bandwidth estimation
- [ ] Sender-side BBR-like algorithm
- [ ] Receiver-side feedback
- [ ] Rate adaptation
- [ ] Weak network resistance optimizations

## Phase 9: Peer Connection

- [ ] Peer connection state machine
- [ ] Integration of all modules
- [ ] Public API implementation
- [ ] Call flow testing

## Phase 10: Demo & Testing

- [ ] Linux demo: sender application
- [ ] Linux demo: receiver application
- [ ] Test with browser WebRTC
- [ ] Interoperability testing
- [ ] Performance benchmarking
- [ ] Memory leak detection

## Future Improvements

- [ ] SCTP data channel support
- [ ] BBR congestion control
- [ ] Additional codec support
- [ ] RTSP integration
- [ ] SIMULCAST support
