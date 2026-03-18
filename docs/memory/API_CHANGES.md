# API Changes - TinyRTC

This file records backward-incompatible API changes and significant additions.

## Table of Contents

- [v0.1.0 - Initial Release (2026-03-16)](#v010---initial-release-2026-03-16)

---

## v0.1.0 - Initial Release (2026-03-16)

This is the first complete API after phase 1-9 development.

### Public API (`include/tinyrtc/`)

| Header | API | Status |
|--------|-----|--------|
| `tinyrtc.h` | `tinyrtc_get_default_config()` | ✅ New |
| `tinyrtc.h` | `tinyrtc_init()` | ✅ New |
| `tinyrtc.h` | `tinyrtc_destroy()` | ✅ New |
| `tinyrtc.h` | `tinyrtc_process_events()` | ✅ New |
| `peer_connection.h` | `tinyrtc_peer_connection_create()` | ✅ New |
| `peer_connection.h` | `tinyrtc_peer_connection_destroy()` | ✅ New |
| `peer_connection.h` | `tinyrtc_peer_connection_create_offer()` | ✅ New |
| `peer_connection.h` | `tinyrtc_peer_connection_set_remote_description()` | ✅ New |
| `peer_connection.h` | `tinyrtc_peer_connection_add_track()` | ✅ New |
| `peer_connection.h` | `tinyrtc_track_send_rtp()` | ✅ New |
| `peer_connection.h` | `tinyrtc_peer_connection_get_stats()` | ✅ New |
| `signaling.h` | `tinyrtc_sdp_parse()` | ✅ New |
| `signaling.h` | `tinyrtc_sdp_generate_offer()` | ✅ New |
| `signaling.h` | `tinyrtc_sdp_generate_answer()` | ✅ New |
| `media_codec.h` | `tinyrtc_codec_manager_init()` | ✅ New |
| `media_codec.h` | `tinyrtc_codec_manager_add_codec()` | ✅ New |

### Key Design Points

- All public APIs use C-style opaque handles (`tinyrtc_context_t*`, `tinyrtc_peer_connection_t*`)
- Callbacks are used for asynchronous events (connection state change, incoming RTP, etc.)
- Configuration is done via structs with `_get_default_config()` helpers
- Strings allocated by TinyRTC must be freed by caller (or are freed when handle is destroyed)

### Not Implemented (yet)

- Data channel API (planned future extension)
- BBR congestion control API (currently only AIMD)
- NACK/PLI feedback API
- Multiple peer connections (API supports it, but not heavily tested)

---

## Notes

- This is an early development version (v0.1.x)
- API may still change based on testing feedback
- We aim to keep backward compatibility once reaching v1.0
