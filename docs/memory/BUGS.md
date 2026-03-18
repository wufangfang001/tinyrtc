# Known Bugs - TinyRTC

This file tracks known bugs, issues, and limitations. Bugs marked [fixed] are kept for historical reference.

## Table of Contents

- [Active Issues](#active-issues)
- [Fixed Issues](#fixed-issues)

---

## Active Issues

| Issue | Module | Severity | Reported | Notes |
|-------|--------|----------|----------|-------|
| **No unit tests** | All | Medium | 2026-03-16 | Need to add unit test infrastructure and tests for each module |
| **TURN not implemented** | ICE | Low | 2026-03-16 | Only placeholder exists, TURN client not implemented |
| **SCTP/Data channel not implemented** | All | Low | 2026-03-16 | Planned for future development |
| **NACK/PLI RTCP feedback not implemented** | RTP | Medium | 2026-03-16 | Needed for good congestion control on lossy networks |
| **Interoperability not fully tested** | All | Medium | 2026-03-16 | Only tested with Chrome browser, need more testing with Firefox/Safari |
| **Memory leak checking incomplete** | All | Medium | 2026-03-16 | Haven't done full leak check with valgrind |
| **MTU discovery not implemented** | ICE/RTP | Low | 2026-03-16 | Currently uses fixed 1200 MTU |
| **No ICE restart support** | ICE | Low | 2026-03-16 | Can't restart ICE after connection failure |

---

## Fixed Issues

| Issue | Module | Fixed in | Date | Description |
|-------|--------|----------|------|-------------|
| *None yet* | | | | First release, no fixes yet |

---

## Contributing

If you find a new bug, please add it to this file with:
- Clear description
- How to reproduce
- Module affected
- Severity assessment

When fixing, move it to Fixed Issues with the fix date.
