# API Changes - API变更历史

This file records backward-incompatible API changes and major feature additions.

## [Unreleased] - 2026-03-18

### Added
- Initial public API: `tinyrtc_init()`, `tinyrtc_destroy()`
- PeerConnection API: `tinyrtc_peer_connection_create()`, `tinyrtc_peer_connection_destroy()`
- Track management: `tinyrtc_peer_connection_add_track()`, `tinyrtc_track_send_rtp()`
- SDP: `tinyrtc_peer_connection_create_offer()`, `tinyrtc_peer_connection_set_remote_description()`
- Event processing: `tinyrtc_process_events()`

### Changed
- N/A (initial release)

### Removed
- N/A (initial release)

---

## Template for future changes:

```
## [Release] - YYYY-MM-DD

### Added
- New features

### Changed
- Changes to existing functionality

### Removed
- Removed APIs
```
