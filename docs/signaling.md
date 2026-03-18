# Signaling Module - 自动信令模块

## 功能描述

该模块实现了基于公共WebSocket信令服务器的自动SDP交换功能，使得demo可以自动完成offer/answer交换，无需手动复制粘贴SDP文件。

## 公共信令服务器信息

- **服务器地址**: `wss://wsnodejs.nodejitsu.com:443`
- **来源**: Muaz Khan's WebRTC-Experiment 项目提供的公共测试服务器
- **协议**: WebSocket + JSON (遵循WebRTC-Experiment约定的房间消息格式)
- **使用条款**: 仅供demo测试使用，不保证长期可用性

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
│  signaling.c - complete implementation of WebSocket client                           │
│  - Handshake complete according to RFC6455                                         │
│  - Supports fragmentation, masking, ping/pong                                      │
│  - Uses mbedTLS for TLS (wss://)                                                  │
├─────────────────────────────────────────────────────────────────────────────────────┤
│                 ↓                                                                   │
└─────────────────────────────────────────────────────────────────────────────────────┘
                          Network --> wss://wsnodejs.nodejitsu.com:443
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
./tinyrtc_send --room test-room-123
```

2. 浏览器打开 `tools/browser_test.html`:
   - 输入房间ID `test-room-123`
   - 点击"Start Automatic Signaling"
   - 等待连接建立，浏览器会自动获取camera/mic并开始传输

### 测试方式二: C++ receiver <-> browser sender

1. 终端运行receiver:
```bash
cd build/demo
./tinyrtc_recv --room test-room-123
```

2. 浏览器打开 `tools/browser_test.html` 输入相同房间ID，自动连接

## 协议消息格式

遵循WebRTC-Experiment约定的格式:

```json
{
  "sender": "client-id",
  "channel": "room-id",
  "message": {
    "type": "offer/answer/candidate",
    "sdp": "...sdp content...",
    "candidate": "...candidate..."
  }
}
```

## 依赖

- mbedTLS (已经作为子模块集成在项目中)
- AOSL (already integrated)

## 注意事项

1. **公共服务器**: 这是第三方提供的公共测试服务器，不保证可用性和稳定性
2. **安全性**: 信令内容通过wss加密，但没有额外应用层加密
3. **ICE**: 当前实现只交换offer/answer，完整ICE candidate交换需要TinyRTC core支持
