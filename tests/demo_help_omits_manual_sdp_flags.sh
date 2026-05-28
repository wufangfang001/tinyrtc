#!/usr/bin/env bash
set -euo pipefail

sender_bin="$1"
recv_bin="$2"

sender_help="$("${sender_bin}" --help)"
recv_help="$("${recv_bin}" --help)"

if grep -q -- "--with-answer" <<<"${sender_help}"; then
    echo "sender help still exposes --with-answer"
    echo "${sender_help}"
    exit 1
fi

if grep -q -- "--offer" <<<"${recv_help}"; then
    echo "receiver help still exposes --offer"
    echo "${recv_help}"
    exit 1
fi

exit 0
