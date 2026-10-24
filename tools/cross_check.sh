#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tooling=/workspace/lpc1768-tooling
compiler="$tooling/toolchain/usr/bin/arm-none-eabi-gcc"
mkdir -p build
for polarity in 1 0; do
    # Newer CMSIS headers require C99. traffic.c itself passes strict C90
    # in tools/test.sh; no target framework or mbed runtime is linked here.
    "$compiler" -mcpu=cortex-m3 -mthumb -std=c99 -Wall -Wextra -Werror \
        -Wdeclaration-after-statement -ffreestanding -Os \
        -DLED_ACTIVE_HIGH="$polarity" -isystem "$tooling" \
        -c traffic.c -o "build/traffic-$polarity.o"
done
"$tooling/toolchain/usr/bin/arm-none-eabi-size" build/traffic-1.o build/traffic-0.o

# Compile the proposed combined trainer wiring, without claiming it was flashed.
"$compiler" -mcpu=cortex-m3 -mthumb -std=c99 -Wall -Wextra -Werror \
    -Wdeclaration-after-statement -ffreestanding -Os -DBOARD_TRAINER_WIRING=1 \
    -isystem "$tooling" -c traffic.c -o build/traffic-peripherals.o
