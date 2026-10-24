# Adaptive traffic signal prototype — LPC1768

The application is **[traffic.c](traffic.c)** with **[traffic_io.h](traffic_io.h)**
and **[board_config.h](board_config.h)**.

**Peripheral extension:** see [PERIPHERALS.md](PERIPHERALS.md) for emergency
keypad controls, buzzer bursts, LCD status, seven-segment countdown, and the
combined wiring plan derived from the uploaded trainer manual. Extra peripheral
pins remain inactive until the wiring profile is configured.

The baseline controller uses all eight LEDs on
the ALS-SDA-ARMCTXM3-01, deterministic simulated densities, Timer0 interrupts,
and a non-blocking foreground state machine. No external sensors or displays
are required. The application uses Embedded C and `LPC17xx.h`; the standard
vendor startup assembly and system clock file are the only additional target
support files. The host tests are separate PC programs, never target sources.

The latest eight-LED request governs this implementation. The uploaded pasted
task supplies the original scenarios and timing bands; its older three-LED
allocation is superseded. Sensors remain a future extension. LCD, keypad, emergency mode, buzzer and
seven-segment support are implemented in the configurable peripheral extension. The lab manual supplies board and programming references.

## LED wiring and polarity

| Pin | Function |
|---|---|
| P0.4 | RED for the selected road; also red while idle |
| P0.5 | YELLOW for the selected road |
| P0.6 | GREEN for the selected road |
| P0.7 | Road 1 |
| P0.8 | Road 2 |
| P0.9 | Road 3 |
| P0.10 | Road 4 |
| P0.11 | Scenario change: one 250 ms ON pulse, then OFF |

The manual's LED experiment (printed pages 24–25) labels `FIOSET` as turning an
LED ON and `FIOCLR` as turning it OFF. This supports **active-high**, the default
`LED_ACTIVE_HIGH=1`. The actual board has not been measured here. If your kit
has inverted LED drivers, define `LED_ACTIVE_HIGH=0` in Keil's C/C++ **Define**
field and rebuild/reflash. The same setting controls all eight LEDs. Never use
the test header in `tests/support` as the target header.

Printed page 23 specifies a 10-core FRC cable between **CNA and CNA1**; Appendix
C, printed page 59, assigns CNA to **P0.4–P0.11**. Power off before connecting
the cable. Match the keyed connectors and pin-1 markings; do not offset the
plug or invent a pin order. Use only this LED connection for the demonstration.
Without the peripheral profile, the program leaves LCD, keypad and seven-segment
pins inactive. Sensors and UART remain unused.

These eight physical LEDs need not be colored red/yellow/green: label the three
corresponding positions with their logical signal names. This is one signal
display plus a road identifier. Other roads' independent red signals are not
physically represented.

## Every scenario and expected sequence

Densities are vehicle counts. Nonpositive densities are skipped. Green is 5 s
for 1–3, 10 s for 4–6, and 15 s for 7 or more (capped at 15 s even above 10).
Highest-density unserved road wins; equal densities use the lower road number.
Each eligible road gets one service per round, including its yellow and red
clearance, before a new scenario is loaded. Thus the fixed snapshot cannot
starve a low-density road: even four high-density roads finish in 72 seconds.
Future live arrivals must be integrated deliberately rather than repeatedly
resetting the served mask.

In the table, **G → Y → R** means:

- Only **P0.6** of the signal LEDs ON for the listed green duration.
- Then only **P0.5** ON for **2 seconds**.
- Then only **P0.4** ON for **1 second** of clearance.

The indicated road LED stays ON throughout all three phases. The other three
road LEDs remain OFF. At a change of road, clearance finishes first, then the
old road/signal are switched off before the new road/green are switched on.
Signal LEDs are never commanded ON together. P0.11's pulse can overlap green
or idle and does not extend or block a phase.

| Scenario (R1,R2,R3,R4) | Service order | Selected road | Road indicator | Density | Green | Signal LEDs |
|---|---:|---|---|---:|---:|---|
| 1: (8,3,6,2) | 1 | Road 1 | P0.7 | 8 | 15 s | G → Y → R |
| 1 | 2 | Road 3 | P0.9 | 6 | 10 s | G → Y → R |
| 1 | 3 | Road 2 | P0.8 | 3 | 5 s | G → Y → R |
| 1 | 4 | Road 4 | P0.10 | 2 | 5 s | G → Y → R |
| 2: (1,9,4,7) | 1 | Road 2 | P0.8 | 9 | 15 s | G → Y → R |
| 2 | 2 | Road 4 | P0.10 | 7 | 15 s | G → Y → R |
| 2 | 3 | Road 3 | P0.9 | 4 | 10 s | G → Y → R |
| 2 | 4 | Road 1 | P0.7 | 1 | 5 s | G → Y → R |
| 3: (5,2,10,3) | 1 | Road 3 | P0.9 | 10 | 15 s | G → Y → R |
| 3 | 2 | Road 1 | P0.7 | 5 | 10 s | G → Y → R |
| 3 | 3 | Road 4 | P0.10 | 3 | 5 s | G → Y → R |
| 3 | 4 | Road 2 | P0.8 | 2 | 5 s | G → Y → R |
| 4: (4,4,4,4) | 1 | Road 1 | P0.7 | 4 | 10 s | G → Y → R |
| 4 | 2 | Road 2 | P0.8 | 4 | 10 s | G → Y → R |
| 4 | 3 | Road 3 | P0.9 | 4 | 10 s | G → Y → R |
| 4 | 4 | Road 4 | P0.10 | 4 | 10 s | G → Y → R |
| 5: (0,6,0,2) | 1 | Road 2 | P0.8 | 6 | 10 s | G → Y → R |
| 5 | 2 | Road 4 | P0.10 | 2 | 5 s | G → Y → R |
| 6: (0,0,0,0) | — | None | All road LEDs OFF | 0 | — | P0.4 RED only, 1 s idle |
| 7: (0,0,9,0) | 1 | Road 3 | P0.9 | 9 | 15 s | G → Y → R |

Round lengths are respectively **47, 57, 47, 52, 21, 1 and 18 seconds**.
The complete seven-scenario cycle is **243 seconds (4 min 3 s)**. Scenario 1
starts without a pulse. P0.11 pulses when 1→2, 2→3, 3→4, 4→5, 5→6, 6→7 and
7→1 occur. An all-empty scenario stays red for a measured second instead of
rapidly cycling through scenarios. Startup initializes safe red with no road
indicator, then selects Road 1 on the first foreground pass.

The normal timeline, relative to the first green, is:

| Time | Scenario begins | First indication |
|---:|---:|---|
| 0 s | 1 | Road 1 + green |
| 47 s | 2 | Road 2 + green + 250 ms P0.11 pulse |
| 104 s | 3 | Road 3 + green + pulse |
| 151 s | 4 | Road 1 + green + pulse |
| 203 s | 5 | Road 2 + green + pulse |
| 224 s | 6 | Red, no road + pulse |
| 225 s | 7 | Road 3 + green + pulse |
| 243 s | 1 | Road 1 + green + pulse; repeat |

Firmware polling and oscillator accuracy can add small physical timing
differences. If foreground execution is stalled, a phase may extend; yellow
and clearance are always given their full durations after the stall.

## How traffic.c works

### GPIO and safe changes

`PINSEL0` uses two bits per pin. `gpio_init()` clears just bits 8–23
(`0x00FFFF00`) to select GPIO for P0.4–P0.11. `PINMODE0` gives those pins no
internal pull-up/down; `PINMODE_OD0` selects normal push-pull outputs. `FIOMASK`
is unmasked only for these eight pins and `FIODIR` is ORed with `0x00000FF0`.
Every unrelated field/direction remains intact. Safe red output latches are
loaded before output drivers are enabled.

`leds_write()` accepts a logical ON mask. In active-high mode it first writes
unwanted LEDs to `FIOCLR`, then wanted LEDs to `FIOSET`. Active-low mode first
sets unwanted LEDs high (OFF), then clears wanted LEDs low (ON). These are
direct writes to hardware set/clear registers, not whole-port `FIOPIN` writes
and not read-modify-write on write-only registers. This provides break-before-
make changes. `display_update()` combines exactly one signal, the selected
road bit and the independently timed scenario pulse.

### Timer0, peripheral clock, match and interrupt

The vendor reset handler must call `SystemInit()` before `main()` to establish
the CPU clock. `timer_init()` powers Timer0 via `PCONP[1]`, resets the timer,
and sets **PCLKSEL0[3:2]=00**, explicitly selecting **Timer0 PCLK=CCLK/4**.
`SystemCoreClockUpdate()` then reads the configured clock registers. The code
uses `SystemCoreClock/4`, not a guessed oscillator or CPU frequency.

`CTCR=0` selects timer mode. `PR=0` means TC advances once every **PR+1=1**
peripheral clock cycles. `MR0=round(PCLK/1000)` produces one match per nominal
millisecond. `MCR=3` enables the MR0 interrupt and resets TC on that match.
There is no stop bit. Capture interrupts and external match outputs are off.

For the usual 100 MHz CPU configuration:

```text
CPU CCLK           = 100,000,000 Hz
Timer0 PCLK        = 25,000,000 Hz
PR                 = 0
MR0                = 25,000
tick period        = MR0 × (PR+1) / PCLK = 0.001 s
one nominal second = 1,000 interrupts
```

At 72 MHz CPU, MR0 is 18,000. At 12 MHz CPU, it is 3,000. Inspect the actual
clock variables rather than assuming the default. Typical clocks divide
exactly by 1000; unusual clocks use nearest-integer rounding, with tick error
at most half a PCLK cycle. A zero/invalid tick count leaves safe red and
`STATE_CLOCK_ERROR`. The oscillator value in the system clock file must match
the physical board or all derived timing will be wrong.

The NVIC routes `TIMER0_IRQn` to **`TIMER0_IRQHandler`**, the exact symbol used
by the LPC17xx vector table. Initialization clears old peripheral and pending
NVIC flags, sets priority 2, enables the IRQ, then starts Timer0. The ISR checks
MR0, clears it with **`IR=1`** (write-one-to-clear), and increments only
`system_millis`. It does no scheduling, LED work or delays. Do not leave
interrupts disabled for long periods: multiple timer matches can collapse into
one pending flag, and this simple tick counter cannot recover lost interrupts.

### Non-blocking state machine

```text
SELECT_ROAD → GREEN (5/10/15 s) → YELLOW (2 s) → ALL_RED (1 s)
                  ↑                                  |
                  +------- next unserved road -------+

Round complete → load next scenario + 250 ms indicator pulse
All empty      → IDLE (red/no road, 1 s) → next scenario
```

`traffic_controller()` reads the volatile tick once per call. It compares
unsigned elapsed time with the current phase duration and returns immediately
if that phase has not expired. Unsigned subtraction remains correct across
the approximately 49.7-day 32-bit millisecond wrap for these short intervals.
`remaining_seconds` is the ceiling of the current phase's remaining
milliseconds divided by 1000; yellow and clearance have their own countdowns.

After clearance, the road is marked in `served_mask` and the next road is
selected immediately. Scenario data is loaded only at a round boundary.
`select_next_road()` ignores nonpositive densities and served roads. Its
strict greater-than comparison preserves the first/lower numbered road in a
tie. The green duration is fixed when that service starts. The pulse has its
own timestamp, checked before phase processing, so it never blocks service.

`main()` sleeps with the CMSIS `__WFI()` instruction between controller calls;
Timer0 wakes the CPU every millisecond. This is interrupt-driven sleeping,
not an arbitrary delay loop. All foreground tasks added later must also return
promptly. The code deliberately begins a fresh full yellow/clearance phase
after late polling rather than rushing unsafe transitions to catch up.

### Debug variables

| Variable | Meaning |
|---|---|
| `current_road` | 1–4; 0 for no active road |
| `current_density` | Selected road's density when its green started |
| `current_scenario` | 1–7 |
| `remaining_seconds` | Current green/yellow/clearance/idle countdown |
| `traffic_state` | 0 select, 1 green, 2 yellow, 3 clearance, 4 idle, 5 clock error |
| `traffic[0]` … `traffic[3]` | Road 1 … Road 4 snapshot |
| `system_millis` | ISR millisecond count |
| `served_mask` | Bits 0–3 identify services completed this round |
| `scenario_blink_active` | Whether the scenario pulse is ON |
| `timer_clock_hz`, `timer_match_ticks` | Actual derived timer settings |

These observables are global `volatile` variables. Only the tick is written
by the ISR; the foreground owns the other controller data. `volatile` ensures
debug visibility but is not a synchronization mechanism for future ISR-based
sensor buffers. Watch updates are not an atomic snapshot of all variables.

## Create and build the Keil µVision4 project

1. On the Windows lab computer, create a project folder, for example
   `C:\ESLab\Traffic`. Copy `traffic.c`, `traffic_io.h`, and `board_config.h` into it. Open **Keil µVision4**.
2. Choose **Project → New µVision Project**, save `Traffic.uvproj` in that
   folder, select **NXP (founded by Philips) → LPC1768**, and click OK.
3. Accept adding the device startup assembly when offered. Its usual name is
   **`startup_LPC17xx.s`**. If it is not added, copy the matching startup from
   your installed Keil LPC17xx device support and add it manually. Use exactly
   one startup file and confirm its reset path calls `SystemInit` before
   entering the C runtime. The manual's C lab startup-dialog filename is
   inconsistent; the startup assembly and system C file are distinct files.
4. Copy **`system_LPC17xx.c`** from your installed device support into the
   project folder, then add the local copy. The manual lists the legacy path
   `C:\Keil\ARM\Startup\NXP\LPC17xx\system_LPC17xx.c`; paths vary by install.
   Use the matching vendor CMSIS version for the startup, `LPC17xx.h`, core
   headers and system file. Do not edit Keil's shared system file in place.
5. Right-click **Source Group 1 → Add Files to Group**, add `traffic.c` and
   your local `system_LPC17xx.c`; add the startup assembly if needed. The
   application has just one source file. **Do not add** `tests/test_traffic.c`
   or any other program with a `main()`.
6. Open **Project → Options for Target** (or right-click Target 1):
   - **Target:** LPC1768; Xtal **12.0 MHz**, as specified by this kit's manual.
     Enable **Use MicroLIB** if desired; the firmware does not depend on it.
     Use the device memory defaults, or IROM1 start `0x00000000`, size
     `0x00080000`; IRAM1 start `0x10000000`, size `0x00008000`.
   - **C/C++:** use ARMCC in C mode (the µVision4 default). No C99 setting is
     required for this application. Add the folder containing the installed
     `LPC17xx.h` and matching CMSIS core headers to **Include Paths** if device
     support has not already done so. A legacy device include folder is
     `C:\Keil\ARM\INC\NXP\LPC17xx`; locate the actual installed headers.
     For debugging choose optimization **Level 0**. Leave Define empty for
     active-high, or set `LED_ACTIVE_HIGH=0` for active-low hardware.
   - **Output:** name the executable `Traffic`, choose an `Objects` output
     directory, enable **Create HEX File** and **Debug Information**.
   - **Linker:** retain **Use Memory Layout from Target Dialog**.
7. Inspect your local system clock file's oscillator definition: it should
   match **12,000,000 Hz**. The standard vendor setup commonly produces
   100 MHz CCLK through its PLL; the application also handles other valid CPU
   clocks because it reads `SystemCoreClock`. Merely changing the Target Xtal
   field does not rewrite the system clock code.
8. Choose **Project → Rebuild all target files**. Resolve any missing-header,
   duplicate-startup, `SystemCoreClockUpdate` or vector-symbol errors before
   flashing. The correct target contains startup + system + traffic only.
   Check the Build Output for **0 errors**, review warnings, and find the
   generated **`Objects\Traffic.hex`**. Confirm it was regenerated by this
   build; an object file or `.axf` is not the Flash Magic input.

For Watch inspection: in Options → Debug choose **Use Simulator** for an
available simulator, or your installed hardware debug adapter if you have
one. Start **Debug → Start/Stop Debug Session**, open **View → Watch Windows
→ Watch 1**, and add the variables listed above. Run, pause and inspect.
Simulator support for this device's Timer0/WFI must be checked locally; a
simulator that does not emulate it cannot prove physical timing. Hardware
Watch requires a supported JTAG/SWD adapter; Flash Magic/ISP alone does not
provide that connection. All physical functional checks below use LEDs and
a stopwatch without assuming a debugger or UART terminal.

## Flash Magic and physical trainer test

The following connections/settings come from the manual's printed pages
22–23. Follow the lab's board supervision and connector labels.

1. Power off; connect the LED FRC **CNA ↔ CNA1** and the board's supported
   power supply (**+5 V**, per the manual). Establish the manual's programming
   connection: **9-pin D-SUB cross cable to CN9**, using the lab PC's serial
   port or its supported serial adapter. Determine its actual Windows COM
   number in Device Manager. Do not connect bare TTL UART pins to RS-232.
2. Follow the manual's programming configuration: **SW21 ON** (two-way DIP)
   and **JP3 shorted**, then power the board. Do not infer undocumented switch
   functions or a different jumper layout. If your physical revision differs,
   confirm it with your instructor/board documentation first.
3. Open **Flash Magic** (the manual uses 6.01), choose:
   - Device **LPC1768**; correct **COM port**; baud **9600**.
   - Interface **None (ISP)** in the manual's version, or the corresponding
     serial ISP selection if your version labels it differently.
   - Oscillator **12 MHz**. This is the crystal value, not the PLL CPU clock.
   - **Erase Blocks Used by Hex File**.
   - Browse to the freshly built **`Objects\Traffic.hex`**.
   - Enable **Verify After Programming**.
4. Under **Options → Advanced Options → Communications**, disable **High
   Speed Communications**. The manual also mentions 115200 in this advanced
   page while prescribing 9600 in the main page. Keep the main communication
   baud at 9600 for the documented baseline; that advanced high-speed setting
   is unused with high-speed communication disabled.
5. Under **Advanced Options → Hardware Config**, the manual specifies
   **Use DTR & RTS to control RST & ISP Pin**, **Keep RTS asserted while COM
   Port open**, **T1=50 ms**, **T2=100 ms**. These options require the lab's
   supported cable/adapter to convey modem-control signals; not every USB
   serial adapter does. If synchronization fails, check that connection and
   the documented SW21/JP3 configuration rather than guessing wiring.
6. Click **Start** and wait for programming and verification to succeed.
   Close/disconnect the Flash Magic COM session to release its control lines,
   then press **SW1 reset**, as the manual instructs. If the board stays in
   ISP, consult the kit's run-mode procedure; no undocumented switch changes
   are assumed here.
7. Use a stopwatch or a video timestamp to follow the table. Do not connect
   additional peripherals for this prototype.

| Physical test | Expected result |
|---|---|
| Reset / highest Road 1 | P0.7 + P0.6 for 15 s; then P0.7 + P0.5 for 2 s; then P0.7 + P0.4 for 1 s |
| First change of road | Road 3 (P0.9) receives 10 s green only after Road 1's full red clearance |
| Highest Road 2 (scenario 2) | At about 47 s: P0.8 green 15 s; next roads 4, 3, 1 |
| Equal traffic (scenario 4) | At about 151 s: road indicators P0.7, P0.8, P0.9, P0.10 in order; 10 s green each |
| Some zero roads (scenario 5) | At about 203 s: only P0.8 then P0.10 serviced; P0.7/P0.9 stay OFF |
| All empty (scenario 6) | At about 224 s: P0.4 only among signal/road LEDs for 1 s; P0.11 pulse allowed |
| Single road (scenario 7) | At about 225 s: only P0.9; 15 s green, 2 s yellow, 1 s clearance |
| Scenario change | P0.11 ON for about 0.25 s at every scenario boundary, then OFF; no phase pause |
| Continuous operation | At about 243 s, scenario 1 restarts and the full sequence repeats |
| Safety throughout | One signal LED ON; exactly one road LED during green/yellow/clearance, none during idle |

If most LEDs are ON when they should be OFF, check polarity before changing
logic. If LEDs show no correct pattern, check CNA/CNA1, power, successful HEX
verification and run mode. If timing is consistently wrong, inspect the actual
crystal/system clock configuration, `timer_clock_hz` and `timer_match_ticks`.
Keil pause/step operation can distort physical time; reset and run freely for
stopwatch measurements. A 250 ms pulse may be easier to confirm on video.

## Cloud verification and reproducible tooling

This cloud environment cannot run Windows µVision4/Flash Magic or access your
physical trainer. It prepares firmware development and meaningful PC tests:

```sh
cd /workspace/ES-LAB-MINI-PROJECT-SEM-5
sh tools/setup_cloud.sh
```

`setup_cloud.sh` installs an ARM GCC cross-compiler into
`/workspace/lpc1768-tooling` through signed Debian package metadata without
root access; it downloads pinned NXP/ARM CMSIS headers from ARMmbed's repository
over verified HTTPS and checks their SHA-256 hashes. No mbed framework is used
or linked. Required network hosts are `deb.debian.org` and
`raw.githubusercontent.com`, already included in the package-manager preset.
There are no secret requirements and no live service to start.

For subsequent checks:

```sh
sh tools/test.sh
sh tools/cross_check.sh
```

The first command compiles the actual `traffic.c` through a test-only register
model using strict **C90**, runs a complete 243-second virtual cycle and checks
every millisecond. It covers priority, ties, skipped roads, single/all-empty
traffic, all phase durations, phase countdowns, pulse expiry, repeating rounds,
tick overflow, late polling, invalid clocks, IRQ handling and preservation of
unrelated GPIO/clock fields. Both active-high and active-low builds run.

The second command compiles both polarities for **ARM Cortex-M3/Thumb** against
real pinned `LPC17xx.h`/CMSIS definitions with warnings treated as errors. These
newer CMSIS headers require C99, so that auxiliary GCC check uses C99; the
application itself is separately checked under strict C90. It produces
relocatable objects in ignored `build/`, not a linked target HEX. The Keil
steps above produce the actual flashable image with its matching vector table,
clock initialization and C runtime.

Verified here: **248,255 phase/output checks per polarity**, plus GPIO/Timer0
initialization, IRQ and threshold assertions; both ARM object builds pass.
The model verifies commanded GPIO states and simulated timing; electrical
polarity, physical oscillator accuracy, Keil linking, ISP download and
hardware operation still require the trainer tests above. Host fake registers
do not emulate the entire MCU or the electrical effects of register writes.
Files are created locally; no GitHub push or hardware flash is implied.

## Later extensions

LCD, keypad density entry, emergency preemption, buzzer alerts and seven-segment
countdown are implemented; see [PERIPHERALS.md](PERIPHERALS.md). Real sensors
remain future work: supply a consistent four-road snapshot and preserve per-round
served tracking. Add any future foreground tasks without blocking the millisecond
peripheral refresh loop.
