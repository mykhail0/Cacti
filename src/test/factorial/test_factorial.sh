#!/bin/bash

if [ "$#" -ne 1 ]; then
  echo "Run like: $0 factorial.exe"
  exit 1
fi

SCRIPT_DIRECTORY="$(dirname "$(realpath "${BASH_SOURCE[0]}")")"

for i in $(seq 1 20); do

  out="$(mktemp)"
  echo "$i" | valgrind --leak-check=full -q "$1" >"$out"
  ret_val=$?
  if [ $ret_val -ne 0 ]; then
    echo "$i: Failed"
    exit 1
  fi
  if diff --color=auto <(echo "$i" | python "${SCRIPT_DIRECTORY}/factorial.py") "$out"; then
    echo "$i: Ok"
    rm "$out"
  else
    echo "$i: Failed"
    exit 1
  fi
done
