# Architecture Notes

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
