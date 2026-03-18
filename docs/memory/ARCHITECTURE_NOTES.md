# Architecture Notes - TinyRTC

This file records key architecture decisions and tradeoffs made during development.

## Table of Contents

- [2026-03-16: Initial Modular Design](#2026-03-16-initial-modular-design)
- [2026-03-16: AOSL Platform Abstraction](#2026-03-16-aosl-platform-abstraction)
- [2026-03-16: Single Threaded Main Loop](#2026-03-16-single-threaded-main-loop)
- [2026-03-16: External Crypto Choice (mbed TLS)](#2026-03-16-external-crypto-choice-mbed-tls)
- [2026-03-16: No Codec Included Design](#2026-03-16-no-codec-included-design)
- [2026-03-16: Application Handles Signaling](#2026-03-16-application-handles-signaling)

---

## 2026-03-16: Initial Modular Design

**Decision:** Split the stack into clearly separated modules by functionality:
- `signal/` - SDP parsing/generation
- `ice/` - ICE/STUN/TURN
- `media/` - RTP/RTCP/DTLS/SRTP
- `net/` - Network abstraction
- `cc/` - Congestion control
- Top level `peer_connection.c` integrates everything

**Rationale:**
- Clear separation of concerns
- Easier to test individual modules
- Easier to maintain and modify
- Allows conditional compilation for resource constrained systems

**Tradeoff:**
- Somewhat more complex integration at the top level
- More function calls between modules (acceptable on modern CPUs)

---

## 2026-03-16: AOSL Platform Abstraction

**Decision:** All platform-specific operations go through AOSL (Advanced Operating System Layer):
- Memory allocation: `aosl_malloc`/`aosl_free`
- Atomic operations: `aosl_atomic_*`
- Threading: `aosl_thread_*` (mutex, rwlock, condition variable, event)
- Network: `aosl_socket_*` + async MPQ (message queue) for event-driven I/O
- Time: `aosl_tick_*` + `aosl_mpq_timer` for timers
- Logging: `aosl_log` leveled logging
- Message queue: `aosl_mpq` for async task scheduling with priorities
- Thread pool: `aosl_mpqp` for CPU-intensive work offload
- Data structures: linked list, red-black tree, packet buffer

**Rationale:**
- TinyRTC focuses on the WebRTC protocol stack only
- Platform porting is handled by AOSL
- No need to reinvent the wheel for each platform
- Already supports Linux, RTOS, etc.

**Tradeoff:**
- Adds a dependency
- But it's a lightweight submodule, not a heavy framework

---

## 2026-03-16: Single Threaded Main Loop

**Decision:** Default to single-threaded operation, application calls `tinyrtc_process_events()` periodically.

**Rationale:**
- Simpler to implement correctly
- Easier to debug
- Lower memory footprint
- Works well on RTOS systems
- Can still offload network I/O to separate threads if needed via AOSL

**Tradeoff:**
- Maximum parallelism limited on multi-core systems
- But WebRTC processing per connection isn't that CPU heavy for most use cases
- Can be optimized later if needed

---

## 2026-03-16: External Crypto Choice (mbed TLS)

**Decision:** Use mbed TLS 2.28 LTS for DTLS handshake and crypto operations.

**Rationale:**
- mbed TLS is widely used and well tested
- Lightweight footprint suitable for embedded systems
- Supports DTLS 1.2 which is required by WebRTC
- Provides all necessary crypto primitives: ECDSA, AES-GCM, SHA-256
- 2.28 is LTS and API stable

**Tradeoff:**
- Adds external dependency (as git submodule)
- But crypto is hard to get right - better to use a proven library
- mbed TLS 3.x has breaking API changes, we stick with 2.28 for compatibility

---

## 2026-03-16: No Codec Included Design

**Decision:** TinyRTC **does not** include any audio/video codec implementations. Applications must provide encoded RTP packets.

**Rationale:**
- Codecs are platform and application specific
- Many systems already have codec integration (FFmpeg, etc.)
- Keeps TinyRTC smaller and more focused
- Allows application to choose appropriate codec based on hardware capabilities
- Easier to integrate into existing systems

**Tradeoff:**
- Application needs to do more work
- But that's the right separation of concerns - TinyRTC handles the connection/packeting, not encoding/decoding

---

## 2026-03-16: Application Handles Signaling

**Decision:** TinyRTC does not include signaling transport. Application must exchange SDP and ICE candidates via its own signaling channel.

**Rationale:**
- Signaling can be implemented in many ways (WebSocket, HTTP, SMS, etc.)
- Different applications have different requirements
- Keeping signaling out makes TinyRTC more flexible

**Tradeoff:**
- Application needs to implement signaling
- But we provide an example simple signaling server in `tools/`

---

## Future Architecture Considerations

- **BBR Congestion Control:** May add as alternative to AIMD
- **SCTP Data Channels:** Possible future extension
- **Hardware Acceleration Hooks:** May need to add for crypto/codec offload
