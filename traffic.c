/* Eight-LED adaptive traffic prototype, LPC1768 / Keil ARMCC (C90).
 * Application includes traffic_io.h and board_config.h; use vendor startup
 * and system_LPC17xx.c. Timer0 and P0.4-P0.11 are reserved by this program.
 */
#include <LPC17xx.h>

#ifndef LED_ACTIVE_HIGH
#define LED_ACTIVE_HIGH 1
#endif
#if (LED_ACTIVE_HIGH != 0) && (LED_ACTIVE_HIGH != 1)
#error LED_ACTIVE_HIGH must be 0 or 1
#endif

#define RED_LED       (1UL << 4)
#define YELLOW_LED    (1UL << 5)
#define GREEN_LED     (1UL << 6)
#define ROAD_LEDS     (0x0FUL << 7)
#define SCENARIO_LED  (1UL << 11)
#define SIGNAL_LEDS   (RED_LED | YELLOW_LED | GREEN_LED)
#define ALL_LEDS      (SIGNAL_LEDS | ROAD_LEDS | SCENARIO_LED)
#define PIN_FIELDS    0x00FFFF00UL
#define ROAD_COUNT    4
#define SCENARIO_COUNT 7
#define YELLOW_MS      2000UL
#define CLEARANCE_MS   1000UL
#define EMPTY_MS       1000UL
#define SCENARIO_PULSE_MS 250UL

typedef enum {
    STATE_SELECT_ROAD = 0,
    STATE_GREEN = 1,
    STATE_YELLOW = 2,
    STATE_ALL_RED = 3,
    STATE_IDLE = 4,
    STATE_CLOCK_ERROR = 5,
    STATE_EMERGENCY_GREEN = 6
} TrafficState;

/* Watch variables: road/scenario are ONE-BASED; road 0 means no active road.
 * traffic[0] is Road 1. Density is the scenario snapshot, not decremented.
 * remaining_seconds counts down the CURRENT phase, rounded upward.
 */
volatile int traffic[ROAD_COUNT] = {8, 3, 6, 2};
volatile int current_road = 0;
volatile int current_density = 0;
volatile int current_scenario = 1;
volatile int remaining_seconds = 0;
volatile int traffic_state = STATE_SELECT_ROAD;
volatile uint32_t system_millis = 0;
volatile uint32_t timer_clock_hz = 0;
volatile uint32_t timer_match_ticks = 0;
volatile uint32_t served_mask = 0;
volatile int scenario_blink_active = 0;

static const int scenarios[SCENARIO_COUNT][ROAD_COUNT] = {
    {8, 3, 6, 2},
    {1, 9, 4, 7},
    {5, 2, 10, 3},
    {4, 4, 4, 4},  /* Equal densities: lower road number wins the tie. */
    {0, 6, 0, 2},  /* Empty roads are skipped. */
    {0, 0, 0, 0},  /* Red idle for one second, then the next scenario. */
    {0, 0, 9, 0}   /* Only Road 3 has vehicles. */
};

static uint32_t phase_started_ms = 0;
static uint32_t phase_duration_ms = 0;
static uint32_t blink_started_ms = 0;
static uint32_t signal_mask = RED_LED;

/* Emergency requests are one-based; zero means no request. The round's
 * served mask survives preemption; the interrupted road receives a fresh
 * normal service later unless it had already completed clearance. */
volatile int emergency_road = 0;
volatile int emergency_active = 0;
volatile int manual_mode = 0;
static int emergency_cancel, emergency_clearance;
static uint32_t buzzer_phase_started_ms;
static int buzzer_phase_on;
static int input_mode, edit_road, edit_value;
static int pending_mode = -1;
static int manual_density[4], staged_density[4];
#include "traffic_io.h"

static void traffic_key(char key)
{
    int i;
    if (key == 'E') key = '*';
    if (key == 'F') key = '#';
    if (traffic_state == STATE_CLOCK_ERROR) return;
    if (emergency_road || emergency_active || emergency_clearance) {
        if (key == '#') emergency_cancel = 1;
        return; /* Locked until cancellation and exit clearance finish. */
    }
    if (key == 'A') { input_mode = 1; return; }
    if (input_mode == 1) {
        if (key >= '1' && key <= '4') {
            emergency_road = key - '0'; input_mode = 0;
        } else if (key == '*') input_mode = 0;
        return;
    }
    if (key == 'B') {
        input_mode = 2; edit_road = edit_value = 0; return;
    }
    if (key == 'C') { pending_mode = 0; input_mode = 0; return; }
    if (input_mode == 2) {
        if (key >= '0' && key <= '9') {
            if (edit_value * 10 + key - '0' <= 99)
                edit_value = edit_value * 10 + key - '0';
        } else if (key == '*') edit_value = 0;
        else if (key == '#') {
            staged_density[edit_road++] = edit_value; edit_value = 0;
            if (edit_road == 4) {
                for (i = 0; i < 4; ++i) manual_density[i] = staged_density[i];
                pending_mode = 1; input_mode = 0;
            }
        }
    }
}

static void text_line(int row, const char *text)
{
    int i;
    for (i = 0; i < 16 && text[i]; ++i) lcd_text[row][i] = text[i];
    for (; i < 16; ++i) lcd_text[row][i] = ' ';
    lcd_text[row][16] = '\0';
}
static void text_number(int row, int col, int value)
{
    if (value < 0) value = 0;
    if (value > 99) value = 99;
    lcd_text[row][col] = (char)('0' + value / 10);
    lcd_text[row][col + 1] = (char)('0' + value % 10);
}
static void buzzer_update(uint32_t now)
{
    uint32_t elapsed, phases;
    buzzer_on = 0;
    if (!emergency_active || traffic_state != STATE_EMERGENCY_GREEN) return;
    /* Advance a rolling half-period, never the age of the whole emergency.
     * No catch-up loop: skipped phases are handled in constant time. The
     * millisecond service must not stop for an entire 32-bit timer period.
     * Late polling cannot recreate missed physical transitions. */
    elapsed = (uint32_t)(now - buzzer_phase_started_ms);
    phases = elapsed / 250UL;
    buzzer_phase_started_ms += phases * 250UL;
    if (phases & 1UL) buzzer_phase_on = !buzzer_phase_on;
    buzzer_on = BUZZER_CONTINUOUS || buzzer_phase_on;
}

static void ui_update(uint32_t now)
{
    segment_value = emergency_active || traffic_state == STATE_CLOCK_ERROR ?
                    -1 : remaining_seconds;
    buzzer_update(now);
    if (traffic_state == STATE_CLOCK_ERROR) {
        text_line(0, "CLOCK ERROR"); text_line(1, "SAFE RED");
    } else if (emergency_road || emergency_active) {
        text_line(0, "EMERGENCY ROAD 0"); lcd_text[0][15] = (char)('0' + emergency_road);
        text_line(1, emergency_active ? "GREEN  F CANCEL" : "WAIT CLEARANCE");
    } else if (input_mode == 1) {
        text_line(0, "EMERGENCY ROAD?"); text_line(1, "1-4  E BACK");
    } else if (input_mode == 2) {
        text_line(0, "SET ROAD 1: 00"); lcd_text[0][9] = (char)('1' + edit_road);
        text_number(0, 12, edit_value); text_line(1, "F NEXT  E CLEAR");
    } else {
        text_line(0, "Road 0  Cars:00"); lcd_text[0][5] = (char)('0' + current_road);
        text_number(0, 13, current_density);
        text_line(1, traffic_state == STATE_GREEN ? "GREEN   Left:00s" :
                     traffic_state == STATE_YELLOW ? "YELLOW  Left:00s" : "RED     Left:00s");
        text_number(1, 13, remaining_seconds);
        if (pending_mode >= 0) text_line(0, "INPUT QUEUED");
    }
}

/* Logical 'on' mask is independent of electrical polarity. First turn off
 * every LED not wanted, then turn on the wanted LEDs: break before make.
 * Writes never include GPIO pins outside P0.4-P0.11.
 */
static void leds_write(uint32_t on_mask)
{
    on_mask &= ALL_LEDS;
#if LED_ACTIVE_HIGH
    LPC_GPIO0->FIOCLR = ALL_LEDS & ~on_mask;
    LPC_GPIO0->FIOSET = on_mask;
#else
    LPC_GPIO0->FIOSET = ALL_LEDS & ~on_mask;
    LPC_GPIO0->FIOCLR = on_mask;
#endif
}

static void display_update(void)
{
    uint32_t mask;
    mask = signal_mask;
    if (current_road >= 1 && current_road <= ROAD_COUNT) {
        mask |= 1UL << (current_road + 6);
    }
    if (scenario_blink_active) {
        mask |= SCENARIO_LED;
    }
    leds_write(mask);
}

void gpio_init(void)
{
    /* Two function/mode bits per pin; preserve every unrelated field. */
    LPC_PINCON->PINSEL0 &= ~PIN_FIELDS;
    LPC_PINCON->PINMODE0 = (LPC_PINCON->PINMODE0 & ~PIN_FIELDS)
                           | 0x00AAAA00UL; /* 10 = neither pull-up nor down. */
    LPC_PINCON->PINMODE_OD0 &= ~ALL_LEDS;
    LPC_GPIO0->FIOMASK &= ~ALL_LEDS;
    /* Load a safe red latch before enabling the output drivers. */
    leds_write(RED_LED);
    LPC_GPIO0->FIODIR |= ALL_LEDS;
}

int timer_init(void)
{
    NVIC_DisableIRQ(TIMER0_IRQn);
    LPC_SC->PCONP |= 1UL << 1; /* Power Timer0. */
    LPC_TIM0->TCR = 2UL;      /* Stop and reset TC/PC. */
    /* PCLKSEL0[3:2] = 00: Timer0 PCLK = CCLK / 4. */
    LPC_SC->PCLKSEL0 &= ~(3UL << 2);
    SystemCoreClockUpdate();
    timer_clock_hz = SystemCoreClock / 4UL;
    /* Rounded nearest tick count. At 100 MHz CCLK: 25,000 counts/ms.
     * PR=0 counts each PCLK. Normal supported clocks divide exactly by 1000.
     */
    timer_match_ticks = (timer_clock_hz + 500UL) / 1000UL;
    if (timer_match_ticks == 0UL) {
        return 0;
    }
    LPC_TIM0->CTCR = 0UL; /* Timer mode, not external counter mode. */
    LPC_TIM0->PR = 0UL;
    LPC_TIM0->MR0 = timer_match_ticks;
    LPC_TIM0->MCR = 3UL;  /* MR0 interrupt + reset TC; do not stop. */
    LPC_TIM0->CCR = 0UL;  /* No capture interrupts. */
    LPC_TIM0->EMR = 0UL;  /* No external match output. */
    LPC_TIM0->IR = 0x3FUL; /* W1C: clear all old timer/capture flags. */
    system_millis = 0UL;
    NVIC_ClearPendingIRQ(TIMER0_IRQn);
    NVIC_SetPriority(TIMER0_IRQn, 2UL);
    NVIC_EnableIRQ(TIMER0_IRQn);
    LPC_TIM0->TCR = 1UL;
    return 1;
}

void TIMER0_IRQHandler(void)
{
    if ((LPC_TIM0->IR & 1UL) != 0UL) {
        LPC_TIM0->IR = 1UL; /* Write ONE to clear MR0 interrupt, not |=. */
        ++system_millis;
    }
}

void load_traffic_scenario(void)
{
    int i;
    for (i = 0; i < ROAD_COUNT; ++i) {
        traffic[i] = scenarios[current_scenario - 1][i];
    }
    served_mask = 0UL;
}

int select_next_road(void)
{
    int i;
    int selected;
    int highest;
    selected = -1;
    highest = 0;
    for (i = 0; i < ROAD_COUNT; ++i) {
        if ((served_mask & (1UL << i)) == 0UL && traffic[i] > highest) {
            highest = traffic[i];
            selected = i;
        }
    }
    return selected; /* Zero-based internal index, -1 if none eligible. */
}

int calculate_green_time(int density)
{
    if (density <= 0) {
        return 0;
    }
    if (density <= 3) {
        return 5;
    }
    if (density <= 6) {
        return 10;
    }
    return 15; /* Also caps any future density above 10. */
}

static void phase_begin(int state, uint32_t duration, uint32_t now)
{
    traffic_state = state;
    phase_started_ms = now;
    phase_duration_ms = duration;
    remaining_seconds = (int)((duration + 999UL) / 1000UL);
    if (state == STATE_GREEN || state == STATE_EMERGENCY_GREEN) {
        signal_mask = GREEN_LED;
    } else if (state == STATE_YELLOW) {
        signal_mask = YELLOW_LED;
    } else {
        signal_mask = RED_LED;
    }
    display_update();
}

static void scenario_advance(uint32_t now)
{
    ++current_scenario;
    if (current_scenario > SCENARIO_COUNT) {
        current_scenario = 1;
    }
    if (pending_mode >= 0) { manual_mode = pending_mode; pending_mode = -1; }
    if (manual_mode) {
        int i;
        for (i = 0; i < 4; ++i) traffic[i] = manual_density[i];
        served_mask = 0;
    } else load_traffic_scenario();
    scenario_blink_active = 1;
    blink_started_ms = now;
}

static void start_next_road(uint32_t now)
{
    int road;
    road = select_next_road();
    if (road < 0) {
        /* Completed round. Advance once only, even if the next is empty. */
        scenario_advance(now);
        road = select_next_road();
    }
    if (road < 0) {
        current_road = 0;
        current_density = 0;
        phase_begin(STATE_IDLE, EMPTY_MS, now);
    } else {
        current_road = road + 1;
        current_density = traffic[road];
        phase_begin(STATE_GREEN,
                    (uint32_t)calculate_green_time(current_density) * 1000UL,
                    now);
    }
}

static void emergency_begin(uint32_t now)
{
    current_road = emergency_road;
    current_density = traffic[current_road - 1];
    emergency_active = 1;
    buzzer_phase_started_ms = now;
    buzzer_phase_on = 1;
    phase_begin(STATE_EMERGENCY_GREEN, 0, now);
}

void traffic_controller(void)
{
    uint32_t now;
    uint32_t elapsed;
    now = system_millis; /* Aligned 32-bit Cortex-M3 read is atomic. */
    /* Independent non-blocking scenario pulse, overlaps the next phase. */
    if (scenario_blink_active &&
        (uint32_t)(now - blink_started_ms) >= SCENARIO_PULSE_MS) {
        scenario_blink_active = 0;
        display_update();
    }
    if (traffic_state == STATE_CLOCK_ERROR) return;
    if (emergency_cancel) {
        emergency_cancel = 0; emergency_road = 0;
        if (emergency_active) {
            emergency_active = 0;
            emergency_clearance = 1;
            phase_begin(STATE_YELLOW, YELLOW_MS, now);
        }
    }
    if (emergency_active) return; /* Indefinite green until explicit cancel. */
    if (emergency_road) {
        if (traffic_state == STATE_GREEN && current_road == emergency_road) {
            emergency_begin(now); return;
        }
        if (traffic_state == STATE_GREEN) {
            emergency_clearance = 1; /* Do not mark interrupted service complete. */
            phase_begin(STATE_YELLOW, YELLOW_MS, now);
            return;
        }
        if (traffic_state == STATE_IDLE || traffic_state == STATE_SELECT_ROAD) {
            emergency_clearance = 1;
            phase_begin(STATE_ALL_RED, CLEARANCE_MS, now);
            return;
        }
    }
    if (traffic_state == STATE_SELECT_ROAD) {
        /* Boot scenario may be empty: show it rather than skipping it. */
        if (select_next_road() < 0) {
            current_road = 0;
            current_density = 0;
            phase_begin(STATE_IDLE, EMPTY_MS, now);
        } else {
            start_next_road(now);
        }
        return;
    }
    /* Unsigned subtraction remains correct across the 32-bit tick wrap.
     * A late foreground pass starts a FULL next phase, never shortens yellow
     * or clearance to 'catch up'. Call at least once per tick in normal use.
     */
    elapsed = (uint32_t)(now - phase_started_ms);
    if (elapsed < phase_duration_ms) {
        remaining_seconds = (int)((phase_duration_ms - elapsed + 999UL)
                                   / 1000UL);
        return;
    }
    remaining_seconds = 0;
    switch (traffic_state) {
    case STATE_GREEN:
        phase_begin(STATE_YELLOW, YELLOW_MS, now);
        break;
    case STATE_YELLOW:
        phase_begin(STATE_ALL_RED, CLEARANCE_MS, now);
        break;
    case STATE_ALL_RED:
        if (!emergency_clearance && current_road >= 1)
            served_mask |= 1UL << (current_road - 1);
        emergency_clearance = 0;
        if (emergency_road) emergency_begin(now);
        else start_next_road(now);
        break;
    case STATE_IDLE:
        start_next_road(now);
        break;
    default:
        current_road = 0;
        current_density = 0;
        phase_begin(STATE_IDLE, EMPTY_MS, now);
        break;
    }
}

/* Boot-only initialization. Leave peripheral GPIO untouched on clock failure.
 * Fault status is debugger-visible; LCD reporting would need a separate timer. */
static int firmware_init(void)
{
    gpio_init();
    load_traffic_scenario();
    if (!timer_init()) {
        traffic_state = STATE_CLOCK_ERROR;
        return 0;
    }
    peripherals_init();
    return 1;
}

static void firmware_step(void)
{
    traffic_controller();
    ui_update(system_millis);
    peripherals_tick(system_millis);
}

int main(void)
{
    /* Inspect the selected vendor startup: it must call SystemInit before
     * main. Use matching startup_LPC17xx.s + system_LPC17xx.c in Keil. */
    if (!firmware_init()) {
        /* Safe red/no road. No keypad row or peripheral output was enabled.
         * The LCD has not started and cannot show the clock error. */
        for (;;) {
            __WFI();
        }
    }
    for (;;) {
        firmware_step();
        __WFI(); /* Sleep until next interrupt; not a software delay. */
    }
}
