#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Run like: $0 matrix.exe"
    exit 1
fi

SCRIPT_DIRECTORY="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"

out="$(mktemp)"
"$1" <"${SCRIPT_DIRECTORY}/matrix.in" >"$out"
ret_val=$?
if [ $ret_val -ne 0 ]; then
    echo "FAIL, code: $ret_val"
    exit 1
fi
if diff --color=auto "${SCRIPT_DIRECTORY}/matrix.out" "$out"; then
    rm "$out"
else
    echo "FAIL, output: $out"
    exit 1
fi
