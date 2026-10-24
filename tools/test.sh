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

# Exercise enabled peripherals with synthetic wiring; never use this map on a kit.
for polarity in 1 0; do
    gcc -std=c90 -pedantic -Wall -Wextra -Werror -O2 \
        -DLED_ACTIVE_HIGH="$polarity" -include tests/support/board_test_config.h \
        -Itests/support tests/test_traffic.c -o "build/test-peripherals-$polarity"
    "./build/test-peripherals-$polarity"
done
