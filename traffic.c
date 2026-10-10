/* Eight-LED adaptive traffic prototype, LPC1768 / Keil ARMCC (C90).
 * Application code is entirely in this file; use the vendor CMSIS startup
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
    STATE_CLOCK_ERROR = 5
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
    if (state == STATE_GREEN) {
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
    load_traffic_scenario();
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
    if (traffic_state == STATE_CLOCK_ERROR) {
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
        served_mask |= 1UL << (current_road - 1);
        start_next_road(now);
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

int main(void)
{
    /* Standard vendor Reset_Handler calls SystemInit before main.
     * Use the matching startup_LPC17xx.s + system_LPC17xx.c in Keil.
     */
    gpio_init();
    load_traffic_scenario();
    if (!timer_init()) {
        traffic_state = STATE_CLOCK_ERROR;
        /* Safe red/no road; debugger can inspect the invalid clock. */
        for (;;) {
            __WFI();
        }
    }
    for (;;) {
        traffic_controller();
        __WFI(); /* Sleep until next interrupt; not a software delay. */
    }
}
