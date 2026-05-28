#!/usr/bin/env bash
set -euo pipefail

repo_root="$1"

matches="$(
    rg -n \
        -e 'manual SDP' \
        -e '手工 SDP' \
        -e 'manual signaling' \
        -e 'offer\.sdp' \
        -e 'answer\.sdp' \
        -e '--with-answer' \
        -e '--offer' \
        -e 'manual mode' \
        -e '手工模式' \
        -e '手工交换' \
        -e 'browser-side WebRTC SDP tool' \
        -e '更可靠的调试基线' \
        "$repo_root/README.md" \
        "$repo_root/docs" \
        "$repo_root/demo" \
        2>/dev/null || true
)"

if [[ -n "${matches}" ]]; then
    echo "found manual SDP residue:"
    echo "${matches}"
    exit 1
fi

exit 0
