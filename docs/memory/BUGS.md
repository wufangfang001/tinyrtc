# Bugs - Known Issues Tracker

This file tracks known bugs, issues, and limitations.

## Critical Issues

None currently known.

## Known Limitations

- [ ] No NACK/PLI RTCP feedback support for packet loss recovery
- [ ] No SCTP data channel support
- [ ] TURN client implementation is a placeholder, not fully implemented
- [ ] Maximum 1 video track and 1 audio track per peer connection currently
- [ ] No BBR congestion control algorithm option, only AIMD
- [ ] No SIMULCAST support
- [ ] Unit test coverage is incomplete

## Interoperability Notes

- Tested with Chrome/Edge browser WebRTC implementation
- H.264 codec negotiation has been tested
- Other codecs need more testing

## Open Questions

- Is the jitter buffer adaptive algorithm optimal for all network conditions?
- Does the congestion control respond quickly enough to sudden bandwidth changes?

## Fixed Issues

<!-- Add fixed issues here with dates -->

