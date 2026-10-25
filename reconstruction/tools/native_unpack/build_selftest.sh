#!/bin/bash
set -e
cd "$(dirname "$0")"
gcc -O2 -o /tmp/bleem_selftest unpack_lib.c selftest.c
/tmp/bleem_selftest
