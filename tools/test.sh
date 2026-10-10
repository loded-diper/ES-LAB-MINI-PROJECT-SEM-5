#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
for polarity in 1 0; do
    gcc -std=c90 -pedantic -Wall -Wextra -Werror -O2 \
        -DLED_ACTIVE_HIGH="$polarity" -Itests/support tests/test_traffic.c \
        -o "build/test-traffic-$polarity"
    "./build/test-traffic-$polarity"
done
