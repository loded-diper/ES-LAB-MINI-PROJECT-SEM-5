# Traffic peripherals and emergency operation

The firmware now includes `traffic_io.h` and `board_config.h`. Keep these beside
`traffic.c` in Keil; compile only `traffic.c`, the vendor startup, and the matching
system clock file. The host test header is never a target header.

## Controls

| Key | Action |
| --- | --- |
| A, then 1–4 | Request emergency green for that road |
| F (or # on a telephone keypad) | Cancel emergency, including a pending request |
| B | Enter four manual densities, road 1 through road 4 |
| 0–9 | Enter a density from 0 to 99 |
| F / # while editing | Confirm this road; after road 4, queue all four values |
| E / * while editing | Clear the current value |
| E / * while choosing emergency road | Leave the selection prompt |
| C | Queue a return to automatic scenarios |

Manual values take effect together at the next round boundary and repeat until
replaced or automatic mode is requested. An empty manual round stays red for one
second. A cancels an unfinished editing interaction and opens emergency selection.
Emergency requests have priority over normal traffic, including zero-density roads.
During a pending/active emergency and its exit clearance, other requests are locked
out. A held key emits one event; release is required before another event. Multiple
simultaneous keys are rejected until release.

A different road's green ends through **2 seconds yellow + 1 second red**. A
request during yellow/red preserves the full existing clearance. A request from
idle/boot waits 1 second red. If the selected road already has green, emergency
green starts immediately. It stays green until F/#; there is no timeout.
Cancellation stops the buzzer and starts full yellow/red clearance. Normal service
then resumes using the preserved served mask. An interrupted normal green is
served afresh later; emergency service does not consume its normal service slot.

The LCD shows road, density and phase countdown, editing prompts, or the emergency
road and cancellation key. The two selected seven-segment digits show seconds,
or `--` during emergency green; the other two digits are unused. During emergency
green only, the buzzer repeats **250 ms ON / 250 ms OFF**. Define
`BUZZER_CONTINUOUS=1` for continuously enabled sound. The driver assumes an active
buzzer module; the manual identifies a control input but does not establish the
buzzer's oscillator or input polarity. Confirm sound and polarity on the trainer.
A passive transducer requires a carrier driver; it is not supported by DC enable.

## Manual evidence and combined wiring

Source: `ARM_LPC-1768_User_Manual (1) (1)_compressed.pdf`, uploaded to this
repository at commit `fad154f` (read from Git without replacing local work).
Printed pages 21–24 describe GPIO connectors; 30–31 LCD; 32 buzzer;
35 keypad; 36 seven-segment; 37 spare connector; 44–46 demo connections.

The manual's examples are **separate experiments**. CNA cannot simultaneously
feed CNA1 (LEDs), CNA2 (segments), and CNA5 (motor/relay/buzzer block) with
independent signals. CNB cannot simultaneously select digits and scan the keypad.
The combined profile below therefore requires additional breakout wiring.
Do not connect the standalone demo cables in parallel.

| Function | Proposed combined connection | GPIO |
| --- | --- | --- |
| Eight LEDs | CNA → CNA1, existing FRC | P0.4–P0.11 |
| LCD | CND → CNAD, existing FRC | D4–D7=P0.23–26, RS=P0.27, E=P0.28 |
| Segment bus | CNC pins 1–8 → CNA2 pins 1–8 | P0.15–P0.22 (a–g, decimal point) |
| Digit selector | CN16 pins 1,2,3,4 → CNB2 pins 1,2,3,4 | P1.20,P1.21,P1.28,P1.29 (BCD A–D) |
| Keypad | CNB → CNB3, existing FRC | P1.23–26 and P2.10–13 |
| Buzzer control only | CN16 pin 5 → CNA5 pin 8 | P3.25 |

Use an appropriate breakout cable for segment data: **CNC pin 9 is P2.13**, also
used by the keypad, so leave it disconnected. Do not wire CNA5 pins 1–7, which
control motors and relays. CN16 pin 9 is VBAT, **not ground**; do not connect it
to a peripheral ground. The on-board peripheral blocks already share board ground.
Use connector labels/pin-1 marks and the manual, with power off while wiring.

For the LCD, the manual specifies CN11, JP16 and JP5 shorted, and POT3 contrast.
It lists CN11 pin 5 as NC; this implementation is write-only and never drives RW.
For a separate generic HD44780 module, tie RW low per its module documentation.
For CNB, JP4 positions 1–2 route P2.13 to CNB pin 8. P2.10 is also the ISP input:
follow the board's run/program procedure, release keypad keys at reset, and ensure
the programming circuitry is not holding a scanned row low during operation.

**Details not supplied by the manual:** CNB3 row/column order, physical key labels
by scan index, CNA2 segment bit order, U7 output-to-digit order and buzzer polarity.
The profile assumes CNB pins 5–8 are rows and 1–4 columns, CNA2 bits 0–7 are a–g/dp,
U7 codes 0/1 select the displayed positions, and active-high buzzer enable. Verify
these against the board or vendor example source before enabling; reverse the
arrays/key map or polarity macros if necessary. These are explicitly provisional,
not measurements. The manual does establish active-high common-cathode segments,
a BCD selector (not direct digit-enable pins), and active-high keypad rows with
pulled-down columns; the drivers follow those interfaces.

`BOARD_TRAINER_WIRING` defaults to **0**, leaving unknown peripheral pins untouched.
After wiring and checking the above, set it to **1** in Keil's Defines or in
`board_config.h`. All pin assignments are centralized there. Alternatively fill
in individual pin arrays for verified custom wiring. The driver rejects partial
assignments, duplicate pins, nonexistent pins and conflicts with P0.4–P0.11 before
configuring any peripheral. Inspect `peripheral_config_error` in the debugger.

## Validation

Run `sh tools/test.sh` for strict C90 host tests with both LED polarities and with
synthetic peripheral wiring enabled. It checks the original 243-second traffic
cycle, emergency entry from green/yellow/red/idle, indefinite hold, cancellation,
priority locking, resume fairness, rollover, editing, LCD content, digit encoding,
debounce and pin-conflict rejection. These are register models, not board emulation.

Run `sh tools/cross_check.sh` for Cortex-M3/Thumb compilation using real CMSIS
headers, including the combined trainer profile. These are object checks, not a
linked HEX or proof of electrical behavior. On hardware, first verify the pin map,
all four keypad rows/columns and key labels, LCD contrast/text, digit order, and
buzzer polarity. Then verify emergency entry/exit clearance with a stopwatch,
250ms buzzer bursts only during emergency green, locked road selection, and normal
resumption. Display refresh, keypad scans and LCD transactions run non-blockingly
from the millisecond foreground loop; do not add blocking tasks to that loop.
