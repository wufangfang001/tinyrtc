#!/usr/bin/env bash
set -euo pipefail

sender_bin="$1"
recv_bin="$2"

sender_help="$("${sender_bin}" --help)"
recv_help="$("${recv_bin}" --help)"

if ! grep -q -- "--room <room-id>" <<<"${sender_help}"; then
    echo "sender help missing --room"
    echo "${sender_help}"
    exit 1
fi

if ! grep -q -- "--server <url>" <<<"${sender_help}"; then
    echo "sender help missing --server"
    echo "${sender_help}"
    exit 1
fi

if grep -q -- "Manual mode:" <<<"${sender_help}"; then
    echo "sender help still references manual mode"
    echo "${sender_help}"
    exit 1
fi

if ! grep -q -- "--room <room-id>" <<<"${recv_help}"; then
    echo "receiver help missing --room"
    echo "${recv_help}"
    exit 1
fi

if ! grep -q -- "--server <url>" <<<"${recv_help}"; then
    echo "receiver help missing --server"
    echo "${recv_help}"
    exit 1
fi

if grep -q -- "Manual mode:" <<<"${recv_help}"; then
    echo "receiver help still references manual mode"
    echo "${recv_help}"
    exit 1
fi

exit 0
