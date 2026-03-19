# API Changes

## 2026-03-19 - Implement ICE candidate handling in demo apps

**文件**：`demo/demo_receiver.c`, `demo/demo_sender.c`

**修改内容**：

- 实现了 `TINYRTC_SIGNAL_EVENT_ICE_CANDIDATE` 事件处理
- 调用 `tinyrtc_peer_connection_add_ice_candidate()` 添加远程ICE candidate
- 之前只是TODO占位，现在连接可以正常建立了

**影响**：
- Fixes connection stuck after offer/answer exchange
- Demo applications now can complete ICE connection and receive media

---

## 2026-03-19 - Fix WebSocket handshake accept-key calculation

**文件**：`src/signaling/signaling.c`

**修改内容**：

1. `sig_perform_websocket_handshake()`:
   - Changed `expected_accept[32]` → `expected_accept[29]`
   - Reason: SHA-1 (20 bytes) always produces exactly 28 base64 characters, need 28 + 1 null = 29 bytes

2. `sig_compute_accept_key()`:
   - Removed unnecessary buffer size checks in all loops (main loop, remaining bits, padding)
   - Since output size is fixed at 28 bytes and we have enough buffer space, checks are redundant
   - This ensures we always generate the full base64 string including padding

**影响**：
- Fixes WebSocket handshake failure when connecting to signaling server
- No API changes, just bug fix
