# Architecture Notes

## 2026-05-25 - Signaling Server Baseline

### Decision

TinyRTC 自动信令的当前基线是第三方 `sdp-transfer` 工程。后续调试、文档更新和联调准备都应以 `sdp-transfer` 为默认前提，不再假设公共 `signal-master` 或 `wsnodejs.nodejitsu.com`。

### Operational Defaults

- Demo 默认信令地址: `ws://localhost:8765`
- 可选 TLS 地址: `wss://localhost:8766`
- 使用自签名 WSS 时: 浏览器先信任证书，TinyRTC demo 使用 `--no-verify`

### Consequences

- 今后的 README、联调说明、调试记录都应优先引用 `sdp-transfer`
- 仓库内遗留的旧公网地址和旧脚本说明都应视为历史信息，不能再作为当前事实
- `sdp-transfer` 的 browser demo 不能直接等同于 TinyRTC 当前已完全兼容的对端实现；它的 payload 形态仍暴露了 TinyRTC 现有解析缺口

## WebSocket Accept-key Calculation

### Decision

For SHA-1 (used in WebSocket handshake accept-key calculation):

- We know SHA-1 always outputs exactly **20 bytes** (160 bits)
- Base64 encoding of 160 bits always produces exactly **28 characters** (with padding)
- Therefore we can safely preallocate a fixed 29-byte buffer (28 + 1 null terminator)
- Remove all runtime buffer boundary checks since they are redundant and caused bugs

### Rationale

- Removing unnecessary checks simplifies code
- Fixed size is guaranteed by SHA-1 algorithm spec, so no risk of buffer overflow
- This bug would have been caught if using fixed size allocation from the beginning

### Consequences

- + Simpler code
- + Fixes handshake failure bug
- + No runtime size check overhead
- - If algorithm changes in future, need to adjust buffer size (very unlikely for WebSocket spec)
