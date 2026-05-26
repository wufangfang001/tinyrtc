# Signaling Module - 自动信令模块

## 功能描述

该模块实现 TinyRTC 与外部 `sdp-transfer` WebSocket 信令服务的自动 SDP 交换，使 demo 可以自动完成 offer/answer 流程，无需手动复制粘贴 SDP 文件。

## 当前基线

- **推荐信令后端**: 第三方工程 `sdp-transfer`
- **默认 WS 地址**: `ws://localhost:8765`
- **默认 WSS 地址**: `wss://localhost:8766`
- **历史引用已废弃**: `wss://signal-master.appspot.com:443`、`wss://wsnodejs.nodejitsu.com:443`

## 架构设计

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│  User Application (demo)                                                           │
├─────────────────────────────────────────────────────────────────────────────────────┤
│                 ↓                                                                   │
│  tinyrtc_signaling_create()  -->  创建信令客户端                                    │
│  tinyrtc_signaling_connect()    -->  连接到信令服务器                                │
│  tinyrtc_signaling_send_offer()  -->  发送offer                                      │
│  tinyrtc_signaling_send_answer()-->  发送answer                                    │
│                 ↓                                                                   │
├─────────────────────────────────────────────────────────────────────────────────────┤
│  signaling.c - WebSocket client implementation                                      │
│  - Handshake complete according to RFC6455                                         │
│  - Supports fragmentation, masking, ping/pong                                      │
│  - Uses mbedTLS for TLS (wss://)                                                   │
├─────────────────────────────────────────────────────────────────────────────────────┤
│                 ↓                                                                   │
└─────────────────────────────────────────────────────────────────────────────────────┘
                    Network --> ws://localhost:8765 | wss://localhost:8766
```

## API 接口

完整API定义参见 `include/tinyrtc/signaling.h`

主要函数:

```c
/* Create signaling client */
tinyrtc_signaling_t *tinyrtc_signaling_create(
    tinyrtc_context_t *ctx,
    const tinyrtc_signaling_config_t *config,
    tinyrtc_signal_callback_t callback,
    void *user_data);

/* Connect to server */
tinyrtc_error_t tinyrtc_signaling_connect(tinyrtc_signaling_t *sig);

/* Send offer */
tinyrtc_error_t tinyrtc_signaling_send_offer(
    tinyrtc_signaling_t *sig,
    const char *to_client_id,
    const char *sdp);

/* Send answer */
tinyrtc_error_t tinyrtc_signaling_send_answer(
    tinyrtc_signaling_t *sig,
    const char *to_client_id,
    const char *sdp);
```

## 示范用法 (demo)

### 测试方式一: C++ sender <-> browser receiver

1. 终端运行sender:
```bash
cd build/demo
./tinyrtc_send --room test-room-123 --server ws://your-server-ip:8765
```

2. 浏览器打开 `sdp-transfer` 自带 Web Demo:
   - 信令地址填 `ws://your-server-ip:8765`
   - 输入房间ID `test-room-123`
   - 点击"Start Automatic Signaling"
   - 等待连接建立，浏览器会自动获取camera/mic并开始传输

### 测试方式二: C++ receiver <-> browser sender

1. 终端运行receiver:
```bash
cd build/demo
./tinyrtc_recv --room test-room-123 --server ws://your-server-ip:8765
```

2. 浏览器打开 `sdp-transfer` 自带 Web Demo:
   - 信令地址填 `ws://your-server-ip:8765`
   - 输入相同房间ID，自动连接

### WSS 自签名证书调试

如果 `sdp-transfer` 使用自签名证书：

1. 先在浏览器访问 `https://your-server-ip:8766` 并手动信任证书
2. 浏览器 Demo 使用 `wss://your-server-ip:8766`
3. TinyRTC demo 命令加 `--no-verify`

## 协议消息格式

当前 `signaling.c` 对接的是 `sdp-transfer` 的扁平 JSON 消息外壳：

```json
{
  "type": "join",
  "room": "room-id"
}
```

```json
{
  "type": "joined",
  "role": "caller|callee",
  "room": "room-id",
  "iceServers": []
}
```

```json
{
  "type": "offer",
  "sdp": "..."
}
```

```json
{
  "type": "answer",
  "sdp": "..."
}
```

```json
{
  "type": "ice-candidate",
  "candidate": "..."
}
```

但要注意：

1. `sdp-transfer` 服务端只是转发消息，不强制把 `sdp` / `candidate` 压平成字符串
2. `sdp-transfer` 自带浏览器 Demo 实际发送的是 `RTCSessionDescription` 和 `RTCIceCandidate` 对象
3. TinyRTC 当前实现只稳定支持字符串形式的 SDP，远端 ICE candidate 对象也还没有完整解析

## 依赖

- mbedTLS (已经作为子模块集成在项目中)
- AOSL (already integrated)
- 外部 `sdp-transfer` 服务（独立维护，不由 TinyRTC 内建）

## 注意事项

1. **当前联调前提**: 默认应从 `sdp-transfer` 出发，不要再按旧公网服务器文档联调
2. **安全性**: 信令内容可通过 WSS 加密，但没有额外应用层加密
3. **对象形态不兼容**: `sdp-transfer` 自带浏览器 Demo 的 `sdp` / `candidate` JSON 形态与 TinyRTC 当前解析能力不完全匹配
4. **ICE**: 当前 `tinyrtc_signaling_send_candidate()` 仍未完成，完整 ICE candidate 收发链路仍需继续补齐
