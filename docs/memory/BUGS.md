# Known Bugs and Fixes

## WebSocket Sec-WebSocket-Accept 验证失败

**问题描述**：tinyrtc客户端连接信令服务器时，总是报 `Invalid Sec-WebSocket-Accept` 错误，握手失败。

**根本原因**：
- 客户端计算 accept key 时，base64编码生成不完整，只生成了部分字符就因为边界检查提前停止，缺少末尾的字符和padding
- 原代码 `expected_accept[32]` 虽然空间足够，但是在 `sig_compute_accept_key()` 中每一步都检查 `j < accept_len - 1`，导致无法生成完整的28字符base64（SHA-1固定需要28字符）

**修复方案**：
1. 将 `expected_accept` 缓冲区大小从 `[32]` 改为 `[29]`，精确容纳28字符base64 + 1个null终止符
2. 在 `sig_compute_accept_key()` 中移除所有不必要的缓冲区边界检查，因为：
   - SHA-1 always outputs exactly 20 bytes → base64 always exactly 28 characters
   - 我们已经分配了29字节缓冲区，足够容纳，不会溢出
3. 保证生成完整的base64字符串，包括最后的padding

**状态**：✅ 已修复

---

## 程序启动即异常退出（段错误/无输出）

**问题描述**：编译后运行 `tinyrtc_recv` 直接退出，没有任何输出。

**根本原因**：
- CMakeLists.txt 中使用了全局 `link_directories` 指定 third_party 库路径
- 导致动态链接器尝试在 `../third_party/mbedtls/library/` 搜索 `libc.so.6`，找不到之后动态链接异常

**修复方案**：重新完整编译，cmake重新配置rpath后解决。

**状态**：✅ 已修复

---

## ICE candidates 没有处理，连接卡住无法建立

**问题描述**：WebSocket握手成功，信令连接建立，offer/answer交换完成，但连接始终无法进入CONNECTED状态，没有音视频帧输出。

**根本原因**：
- demo代码（`demo_receiver.c` 和 `demo_sender.c`）中，对 `TINYRTC_SIGNAL_EVENT_ICE_CANDIDATE` 事件只是TODO占位，没有实际调用API添加ICE candidate
- ICE Agent收不到远程candidate，无法进行连通性检查，所以连接无法建立

**修复方案**：
- 在signaling_callback中处理 `TINYRTC_SIGNAL_EVENT_ICE_CANDIDATE` 事件
- 调用已存在的API `tinyrtc_peer_connection_add_ice_candidate()` 添加远程ICE candidate

**状态**：✅ 已修复
