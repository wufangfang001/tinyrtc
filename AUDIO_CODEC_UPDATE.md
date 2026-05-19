# Audio Codec Update for TinyRTC Demo

## Overview

This update replaces Opus audio support with G.711 (PCMA/PCMU) and G.722 audio codecs in the tinyrtc_send demo.

## Changes Made

### 1. Added G.722 Codec to TinyRTC Core

**File: `include/tinyrtc/media_codec.h`**
- Added `TINYRTC_CODEC_G722 = 103` to the codec enum

**File: `src/media_codec.c`**
- Added G.722 entry to codec info table:
  - Name: "G722"
  - Default payload type: 9 (standard RTP payload type for G.722)
  - Clock rate: 8000 Hz
  - Channels: 1 (mono)

### 2. Updated demo_sender.c

**Changes:**
- Added `--audio-codec` command-line option supporting: g722, pcma, pcmu (default: g722)
- Removed Opus audio file support
- Updated audio file search path to look for `send_audio.{codec}` files
- Audio file search locations:
  1. Current directory: `send_audio.g722`, `send_audio.pcma`, `send_audio.pcmu`
  2. Standard path: `/home/ubuntu/agora_rtsa_sdk/example/out/x86_64/send_audio.{codec}`
- Updated audio frame timing:
  - 20ms frames (50 packets per second)
  - 160 bytes per frame for G.711/G.722 (64 kbps)
  - Timestamp increment calculated dynamically from codec clock rate
  - G.722: actual sampling is 16kHz, RTP timestamp uses 8kHz clock (RFC 3551)
- Fixed duplicate free() bug in cleanup code
- Main loop timing adjusted to 20ms for proper audio pacing

### 3. Updated demo_receiver.c

- Changed default audio codec from OPUS to G722 for consistency

## Usage

### Send with default G.722 audio:
```bash
cd build/demo
./tinyrtc_send --room my-room
```

### Send with PCMA (G.711 A-law):
```bash
./tinyrtc_send --room my-room --audio-codec pcma
```

### Send with PCMU (G.711 μ-law):
```bash
./tinyrtc_send --room my-room --audio-codec pcmu
```

## Audio File Locations

Pre-generated audio files are available at:
- `/home/ubuntu/agora_rtsa_sdk/example/out/x86_64/send_audio.g722`
- `/home/ubuntu/agora_rtsa_sdk/example/out/x86_64/send_audio.pcma`
- `/home/ubuntu/agora_rtsa_sdk/example/out/x86_64/send_audio.pcmu`

## Codec Specifications

**Note on G.722:**
- Actual audio sampling rate: 16000 Hz
- RTP clock rate (per RFC 3551): 8000 Hz (historical convention)
- Frame size: 160 bytes per 20ms frame (same as G.711)

| Codec | Payload Type | RTP Clock Rate | Actual Sampling | Channels | Frame Size (20ms) | Bit Rate |
|-------|-------------|----------------|-----------------|----------|-------------------|----------|
| G.722 | 9           | 8000 Hz        | 16000 Hz        | 1        | 160 bytes         | 64 kbps  |
| PCMA  | 8           | 8000 Hz        | 8000 Hz         | 1        | 160 bytes         | 64 kbps  |
| PCMU  | 0           | 8000 Hz        | 8000 Hz         | 1        | 160 bytes         | 64 kbps  |

## Build

```bash
cd build
cmake ..
make -j4
```
