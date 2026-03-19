# API Changes

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
