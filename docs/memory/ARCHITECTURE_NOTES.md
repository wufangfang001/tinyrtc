# Architecture Notes - 架构决策和权衡记录

This file records key architectural decisions made during development, along with the reasoning and trade-offs considered.

## 2026-03-18: Project Initial Setup

### Decision: Use pure GNU99 C with AOSL platform abstraction

**Options considered:**
1. **Pure C + AOSL** (chosen)
   - Pros: Maximum portability to Linux/RTOS, minimal dependencies, small binary size
   - Cons: More boilerplate than C++, no OOP features
   
2. **C++**
   - Pros: OOP, RAII, better abstractions
   - Cons: Some RTOS compilers have incomplete C++ support, larger binary

**Reasoning:**
TinyRTC targets resource-constrained systems first. Pure C maximizes portability. AOSL provides modern programming primitives (list, rbtree, atomic, threads) that offset many of the disadvantages of pure C.

---

### Decision: mbed TLS 2.28 LTS for crypto

**Options considered:**
1. **mbed TLS 2.28** (chosen)
   - Pros: Actively maintained LTS, small footprint, complete DTLS/SRTP support
   - Cons: 2.x API is older than 3.x
   
2. **mbed TLS 3.x**
   - Pros: Newer API
   - Cons: Incompatible API changes, not all RTOS platforms have updated yet

3. **OpenSSL**
   - Pros: Feature complete
   - Cons: Too large for embedded systems

**Reasoning:**
Size compatibility with embedded systems is more important than latest API. 2.28 is LTS until 2025-03.

---

### Decision: Application provides encoded RTP, no codecs inside TinyRTC

**Options considered:**
1. **No codecs (chosen)** - TinyRTC only handles RTP/ICE/DTLS
   - Pros: Small size, application can use whatever codecs they want (software/hardware)
   - Cons: Application needs to integrate codecs separately
   
2. **include reference software codecs**
   - Pros: Easier for getting started
   - Cons: Increases binary size, duplication with platform codec support

**Reasoning:**
On many embedded platforms, codecs are provided by hardware. Including software codecs would bloat the library unnecessarily.

---

### Decision: Single-threaded main loop with asynchronous processing

**Options considered:**
1. **Single-threaded main loop** (default, chosen)
   - Pros: Simple, no locking overhead, predictable latency
   - Cons: Cannot utilize multiple cores for processing
   
2. **Multi-threaded by design**
   - Pros: Can parallelize work
   - Cons: More complex, requires more synchronization, higher memory usage

**Reasoning:**
Most use cases on embedded/RTOS run TinyRTC on a dedicated core. Single-threaded is simpler and easier to debug. AOSL allows offloading heavy work to thread pools when needed.

---

### Decision: AIMD congestion control as default

**Options considered:**
1. **AIMD** (chosen)
   - Pros: Simple, well-understood, small code footprint
   - Cons: Slower response to bandwidth changes than BBR
   
2. **BBR**
   - Pros: Better throughput on long fat networks
   - Cons: More complex, larger code, requires accurate timing measurements

**Reasoning:**
AIMD is the WebRTC standard default. BBR can be added as an optional plugin later.

---

## 2026-03-18: Unit Testing

### Decision: Integrate minunit for unit testing

**Options considered:**
1. **minunit** (chosen)
   - Pros: Single header file, zero dependencies, extremely simple
   - Cons: Very basic, no fancy features
   
2. **Check** (C unit testing framework)
   - Pros: More features, assertions, memory leak detection
   - Cons: Requires linking, adds build dependency

**Reasoning:**
For a small project like TinyRTC, simplicity is key. minunit is sufficient for our needs and doesn't add any build complexity.

---

