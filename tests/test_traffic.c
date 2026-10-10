/* Executes the actual firmware with modeled registers; not a second controller. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "support/LPC17xx.h"

TestPincon test_pincon;
TestGpio test_gpio;
TestSc test_sc;
TestTimer test_timer;
uint32_t SystemCoreClock = 100000000UL;
static int irq_enabled, irq_pending;
static uint32_t irq_priority;
void SystemCoreClockUpdate(void) {}
void NVIC_DisableIRQ(int irq) { assert(irq == 1); irq_enabled = 0; }
void NVIC_ClearPendingIRQ(int irq) { assert(irq == 1); irq_pending = 0; }
void NVIC_SetPriority(int irq, uint32_t p) { assert(irq == 1); irq_priority = p; }
void NVIC_EnableIRQ(int irq) { assert(irq == 1); irq_enabled = 1; }
void __WFI(void) {}

#define main traffic_firmware_main
#include "../traffic.c"
#undef main

#define UNRELATED (1UL << 20)
static uint32_t pin_levels;
static unsigned long checks;

static uint32_t logical_leds(void)
{
    uint32_t off, on, result;
    assert((test_gpio.FIOSET & ~ALL_LEDS) == 0UL);
    assert((test_gpio.FIOCLR & ~ALL_LEDS) == 0UL);
    assert((test_gpio.FIOSET & test_gpio.FIOCLR) == 0UL);
#if LED_ACTIVE_HIGH
    off = test_gpio.FIOCLR;
    on = test_gpio.FIOSET;
    pin_levels &= ~off;
    assert((pin_levels & SIGNAL_LEDS) == 0UL ||
           (pin_levels & SIGNAL_LEDS) == (on & SIGNAL_LEDS));
    pin_levels |= on;
    result = pin_levels & ALL_LEDS;
#else
    off = test_gpio.FIOSET;
    on = test_gpio.FIOCLR;
    pin_levels |= off;
    pin_levels &= ~on;
    result = (~pin_levels) & ALL_LEDS;
#endif
    assert((off | on) == ALL_LEDS);
    assert((pin_levels & UNRELATED) != 0UL);
    return result;
}

static void check_outputs(int road, int density, int state, int countdown)
{
    uint32_t expected, actual;
    expected = state == STATE_GREEN ? GREEN_LED :
               (state == STATE_YELLOW ? YELLOW_LED : RED_LED);
    if (road != 0) {
        expected |= 1UL << (road + 6);
    }
    if (scenario_blink_active) {
        expected |= SCENARIO_LED;
    }
    actual = logical_leds();
    assert(actual == expected);
    assert(current_road == road);
    assert(current_density == density);
    assert(traffic_state == state);
    assert(remaining_seconds == countdown);
    ++checks;
}

static void tick(void)
{
    test_timer.IR = 1UL;
    TIMER0_IRQHandler();
    traffic_controller();
}

static void reset_controller(int scenario)
{
    memset(&test_pincon, 0, sizeof(test_pincon));
    memset(&test_gpio, 0, sizeof(test_gpio));
    memset(&test_sc, 0, sizeof(test_sc));
    memset(&test_timer, 0, sizeof(test_timer));
    current_road = 0;
    current_density = 0;
    current_scenario = scenario;
    remaining_seconds = 0;
    traffic_state = STATE_SELECT_ROAD;
    scenario_blink_active = 0;
    signal_mask = RED_LED;
    SystemCoreClock = 100000000UL;
    pin_levels = UNRELATED;
    gpio_init();
    load_traffic_scenario();
    assert(timer_init());
    traffic_controller();
}

/* Each row is an independently specified expected service in priority order. */
typedef struct { int scenario, road, density, green; } Service;
static const Service services[] = {
    {1,1,8,15}, {1,3,6,10}, {1,2,3,5}, {1,4,2,5},
    {2,2,9,15}, {2,4,7,15}, {2,3,4,10}, {2,1,1,5},
    {3,3,10,15}, {3,1,5,10}, {3,4,3,5}, {3,2,2,5},
    {4,1,4,10}, {4,2,4,10}, {4,3,4,10}, {4,4,4,10},
    {5,2,6,10}, {5,4,2,5},
    {7,3,9,15}
};

static void hold_phase(int road, int density, int state, unsigned long ms)
{
    unsigned long elapsed;
    for (elapsed = 0; elapsed < ms; ++elapsed) {
        check_outputs(road, density, state,
                      (int)((ms - elapsed + 999UL) / 1000UL));
        tick();
    }
}

static void test_complete_cycle(void)
{
    unsigned int i;
    uint32_t initial;
    const Service *s;
    reset_controller(1);
    initial = system_millis;
    for (i = 0; i < sizeof(services) / sizeof(services[0]); ++i) {
        s = &services[i];
        assert(current_scenario == s->scenario);
        hold_phase(s->road, s->density, STATE_GREEN,
                   (unsigned long)s->green * 1000UL);
        hold_phase(s->road, s->density, STATE_YELLOW, 2000UL);
        hold_phase(s->road, s->density, STATE_ALL_RED, 1000UL);
        if (i == 17) {
            assert(current_scenario == 6);
            hold_phase(0, 0, STATE_IDLE, 1000UL);
        }
    }
    assert(system_millis - initial == 243000UL);
    assert(current_scenario == 1);
    assert(served_mask == 0UL);
    check_outputs(1, 8, STATE_GREEN, 15);
}

static void test_blink_and_wrap(void)
{
    unsigned long i;
    reset_controller(6);
    system_millis = UINT32_MAX - 499UL;
    /* Restart this idle phase at a tick close to overflow. */
    phase_begin(STATE_IDLE, EMPTY_MS, system_millis);
    hold_phase(0, 0, STATE_IDLE, 1000UL);
    assert(current_scenario == 7);
    for (i = 0; i < 250UL; ++i) {
        assert(scenario_blink_active);
        check_outputs(3, 9, STATE_GREEN, 15);
        tick();
    }
    assert(!scenario_blink_active);
    check_outputs(3, 9, STATE_GREEN, 15);
    reset_controller(6);
    hold_phase(0, 0, STATE_IDLE, 1000UL);
    system_millis = UINT32_MAX - 99UL;
    blink_started_ms = system_millis;
    phase_begin(STATE_GREEN, 15000UL, system_millis);
    for (i = 0; i < 250UL; ++i) { tick(); }
    assert(!scenario_blink_active);
    check_outputs(3, 9, STATE_GREEN, 15);
}

static void test_gpio_timer_and_irq(void)
{
    uint32_t previous, unchanged;
    reset_controller(1);
    test_pincon.PINSEL0 = UINT32_MAX;
    test_pincon.PINMODE0 = UINT32_MAX;
    test_pincon.PINMODE_OD0 = UINT32_MAX;
    test_gpio.FIOMASK = UINT32_MAX;
    test_gpio.FIODIR = UNRELATED;
    gpio_init();
    assert(test_pincon.PINSEL0 == (UINT32_MAX & ~PIN_FIELDS));
    assert(test_pincon.PINMODE0 ==
           ((UINT32_MAX & ~PIN_FIELDS) | 0x00AAAA00UL));
    assert(test_pincon.PINMODE_OD0 == (UINT32_MAX & ~ALL_LEDS));
    assert(test_gpio.FIOMASK == (UINT32_MAX & ~ALL_LEDS));
    assert(test_gpio.FIODIR == (UNRELATED | ALL_LEDS));
    test_sc.PCLKSEL0 = UINT32_MAX;
    test_sc.PCONP = UNRELATED;
    irq_pending = 1;
    SystemCoreClock = 72000000UL;
    assert(timer_init());
    assert(test_sc.PCLKSEL0 == (UINT32_MAX & ~(3UL << 2)));
    assert(test_sc.PCONP == (UNRELATED | 2UL));
    assert(timer_clock_hz == 18000000UL);
    assert(test_timer.MR0 == 18000UL);
    assert(test_timer.PR == 0UL && test_timer.MCR == 3UL);
    assert(test_timer.TCR == 1UL && test_timer.CTCR == 0UL);
    assert(irq_enabled && !irq_pending && irq_priority == 2UL);
    previous = system_millis;
    test_timer.IR = 0UL;
    TIMER0_IRQHandler();
    assert(system_millis == previous);
    test_timer.IR = 1UL;
    TIMER0_IRQHandler();
    assert(system_millis == previous + 1UL && test_timer.IR == 1UL);
    SystemCoreClock = 0UL;
    unchanged = test_sc.PCLKSEL0 & ~(3UL << 2);
    assert(!timer_init());
    assert(!irq_enabled && test_timer.TCR == 2UL);
    assert((test_sc.PCLKSEL0 & ~(3UL << 2)) == unchanged);
}

static void test_late_poll_and_thresholds(void)
{
    reset_controller(1);
    system_millis = 50000UL; /* Foreground was stalled past green expiry. */
    traffic_controller();
    check_outputs(1, 8, STATE_YELLOW, 2);
    hold_phase(1, 8, STATE_YELLOW, 2000UL);
    hold_phase(1, 8, STATE_ALL_RED, 1000UL);
    check_outputs(3, 6, STATE_GREEN, 10);
    assert(calculate_green_time(-1) == 0);
    assert(calculate_green_time(0) == 0);
    assert(calculate_green_time(1) == 5 && calculate_green_time(3) == 5);
    assert(calculate_green_time(4) == 10 && calculate_green_time(6) == 10);
    assert(calculate_green_time(7) == 15 && calculate_green_time(10) == 15);
    assert(calculate_green_time(100) == 15);
    reset_controller(1);
    traffic[0] = 0; traffic[1] = -1; traffic[2] = 0; traffic[3] = 0;
    assert(select_next_road() == -1);
}

int main(void)
{
    test_complete_cycle();
    test_blink_and_wrap();
    test_gpio_timer_and_irq();
    test_late_poll_and_thresholds();
    printf("PASS polarity=%d: %lu phase/output checks; seven scenarios, "
           "countdowns, GPIO preservation, timer/IRQ, wrap and late polling\n",
           LED_ACTIVE_HIGH, checks);
    return 0;
}
