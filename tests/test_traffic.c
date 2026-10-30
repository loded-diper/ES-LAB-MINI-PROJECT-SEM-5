/* Executes the actual firmware with modeled registers; not a second controller. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "support/LPC17xx.h"

TestPincon test_pincon;
TestGpio test_gpio, test_extra_gpio[4];
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

static unsigned long peripheral_writes;
static int observed_levels[160];
static void observe_io(int pin, int high)
{
    assert(pin >= 0 && pin < 160);
    observed_levels[pin] = high;
    ++peripheral_writes;
}
#define IO_WRITE_OBSERVER(pin, high) observe_io(pin, high)
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
    emergency_road = emergency_active = emergency_cancel = emergency_clearance = 0;
    manual_mode = input_mode = 0; pending_mode = -1;
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


static void advance_ms(unsigned long count)
{
    while (count--) tick();
}
static void test_emergency(void)
{
    int state;
    reset_controller(1);
    traffic_key('A'); traffic_key('2'); traffic_controller();
    assert(traffic_state == STATE_YELLOW && current_road == 1);
    advance_ms(1999); assert(traffic_state == STATE_YELLOW);
    advance_ms(1); assert(traffic_state == STATE_ALL_RED);
    advance_ms(999); assert(!emergency_active);
    advance_ms(1); assert(emergency_active && current_road == 2);
    assert(traffic_state == STATE_EMERGENCY_GREEN && signal_mask == GREEN_LED);
    ui_update(system_millis);
    assert(buzzer_on && segment_value == -1);
    assert(strcmp(lcd_text[0], "EMERGENCY ROAD 2") == 0);
    advance_ms(249); ui_update(system_millis); assert(buzzer_on);
    advance_ms(1); ui_update(system_millis); assert(!buzzer_on || BUZZER_CONTINUOUS);
    advance_ms(250); ui_update(system_millis); assert(buzzer_on);
    traffic_key('A'); traffic_key('4'); assert(emergency_road == 2);
    advance_ms(60000); assert(emergency_active && current_road == 2);
    traffic_key('#'); traffic_controller(); ui_update(system_millis);
    assert(!buzzer_on && traffic_state == STATE_YELLOW);
    advance_ms(2000); assert(traffic_state == STATE_ALL_RED);
    advance_ms(1000); assert(traffic_state == STATE_GREEN && current_road == 1);
    assert(served_mask == 0); /* Interrupted road not lost. */
    reset_controller(1);
    traffic_key('A'); traffic_key('1'); traffic_controller();
    assert(emergency_active && current_road == 1);
    buzzer_phase_started_ms = UINT32_MAX - 100UL;
    system_millis = 149; ui_update(system_millis);
    assert(!buzzer_on || BUZZER_CONTINUOUS); /* 250ms across wrap. */
    for (state = STATE_YELLOW; state <= STATE_ALL_RED; ++state) {
        reset_controller(1);
        phase_begin(state, state == STATE_YELLOW ? 2000 : 1000, system_millis);
        advance_ms(100);
        traffic_key('A'); traffic_key('4'); traffic_controller();
        assert(traffic_state == state);
        advance_ms(state == STATE_YELLOW ? 2899 : 899);
        assert(!emergency_active);
        advance_ms(1); assert(emergency_active && current_road == 4);
    }
    reset_controller(6);
    traffic_key('A'); traffic_key('3'); traffic_controller();
    assert(traffic_state == STATE_ALL_RED && current_road == 0);
    advance_ms(1000); assert(emergency_active && current_road == 3);
    reset_controller(1);
    traffic_key('A'); traffic_key('2'); traffic_controller();
    traffic_key('#'); traffic_controller();
    advance_ms(3000); assert(!emergency_active && current_road == 1);
    reset_controller(1);
    traffic_state = STATE_CLOCK_ERROR;
    traffic_key('A'); traffic_key('2'); traffic_controller();
    assert(!emergency_road && traffic_state == STATE_CLOCK_ERROR);
    puts("PASS emergency: clearance, hold, lock, cancel, resume, idle, wrap, fault");
}
/* Independent 64-bit oracle, with bounded polling gaps, across more than
 * two full 32-bit periods. Dense checks catch the old 46ms OFF interval. */
static void test_long_buzzer(void)
{
    uint64_t elapsed, edge;
    unsigned int period;
    reset_controller(1);
    traffic_key('A'); traffic_key('1'); traffic_controller();
    elapsed = 0;
    for (period = 1; period <= 2; ++period) {
        edge = (uint64_t)period * ((uint64_t)UINT32_MAX + 1);
        while (elapsed + 1000003UL < edge - 1000) {
            elapsed += 1000003UL; /* Exercise odd/even skipped half periods. */
            system_millis = (uint32_t)elapsed;
            ui_update(system_millis);
            assert(buzzer_on == (BUZZER_CONTINUOUS || elapsed % 500 < 250));
        }
        for (; elapsed < edge + 1000; ++elapsed) {
            system_millis = (uint32_t)elapsed;
            ui_update(system_millis);
            assert(buzzer_on == (BUZZER_CONTINUOUS || elapsed % 500 < 250));
        }
    }
    puts("PASS buzzer: two full timer periods, boundary waveform, delayed polling");
}

static void test_boot_and_buzzer_output(void)
{
    TestGpio before[4];
    unsigned int i;
    reset_controller(1);
    traffic_state = STATE_SELECT_ROAD;
    current_road = current_density = 0;
    SystemCoreClock = 0;
    memcpy(before, test_extra_gpio, sizeof(before));
    peripheral_writes = 0;
    assert(!firmware_init());
    assert(traffic_state == STATE_CLOCK_ERROR && !irq_enabled);
    assert(peripheral_writes == 0);
    assert(memcmp(before, test_extra_gpio, sizeof(before)) == 0);
    assert(logical_leds() == RED_LED);

    reset_controller(1);
    assert(firmware_init());
    if (buzzer_pin[0] < 0) return;
    assert(buzzer_enabled);
    assert(observed_levels[buzzer_pin[0]] == !BUZZER_ACTIVE_HIGH);
    /* Released columns, so scanning does not create unrelated events. */
    for (i = 0; i < 4; ++i) test_extra_gpio[i].FIOPIN =
        KEYPAD_ACTIVE_HIGH ? 0 : UINT32_MAX;
    traffic_key('A'); traffic_key('1');
    ++system_millis; firmware_step();
    assert(observed_levels[buzzer_pin[0]] == BUZZER_ACTIVE_HIGH);
    system_millis += 249; firmware_step();
    assert(observed_levels[buzzer_pin[0]] == BUZZER_ACTIVE_HIGH);
    ++system_millis; firmware_step();
    assert(observed_levels[buzzer_pin[0]] ==
           (BUZZER_CONTINUOUS ? BUZZER_ACTIVE_HIGH : !BUZZER_ACTIVE_HIGH));
    system_millis += 250; firmware_step();
    assert(observed_levels[buzzer_pin[0]] == BUZZER_ACTIVE_HIGH);
    traffic_key('F');
    ++system_millis; firmware_step();
    assert(traffic_state == STATE_YELLOW);
    assert(observed_levels[buzzer_pin[0]] == !BUZZER_ACTIVE_HIGH);
    /* A new emergency resets the envelope to ON. */
    system_millis += 2000; firmware_step();
    system_millis += 1000; firmware_step();
    traffic_key('A'); traffic_key('1');
    ++system_millis; firmware_step();
    assert(observed_levels[buzzer_pin[0]] == BUZZER_ACTIVE_HIGH);
    puts("PASS boot failure leaves peripheral pins untouched; full-loop buzzer output/cancel");
}

static void test_manual_and_ui(void)
{
    int i;
    reset_controller(1);
    traffic_key('B'); traffic_key('9'); traffic_key('*'); traffic_key('2');
    ui_update(system_millis); assert(strncmp(lcd_text[0], "SET ROAD 1: 02", 14) == 0);
    traffic_key('#');
    for (i = 0; i < 3; ++i) { traffic_key('0'); traffic_key('#'); }
    assert(pending_mode == 1 && !manual_mode && traffic[0] == 8);
    advance_ms(47000);
    assert(manual_mode && current_road == 1 && current_density == 2);
    ui_update(system_millis); assert(segment_value == 5);
    assert(strcmp(lcd_text[0], "Road 1  Cars:02 ") == 0);
    advance_ms(8000); assert(manual_mode && current_density == 2);
    traffic_key('C'); advance_ms(8000); assert(!manual_mode);
    assert(seg_pattern(0) == 0x3f && seg_pattern(8) == 0x7f);
    assert(seg_pattern(-1) == 0x40);
    puts("PASS manual density editing, round-boundary application, LCD and digits");
}
static void test_peripherals(void)
{
    uint32_t used[5] = {0,0,0,0,0};
    const int conflict[2] = {50,50};
    const int partial[2] = {50,-1};
    unsigned long t;
    int col, pin;
    reset_controller(1); peripherals_init();
    assert(!peripheral_config_error);
    assert(!io_pin_valid(4) && !io_pin_valid(12) && !io_pin_valid(160));
    assert(io_pin_valid(50));
    key_candidate = -1; key_latched = 0; key_changed = 0;
    keypad_sample(3, 0); keypad_sample(3, 23); assert(!input_mode);
    keypad_sample(3, 24); assert(input_mode == 1);
    input_mode = 0;
    keypad_sample(3, 100); assert(!input_mode); /* No held-key repeats. */
    keypad_sample(-1, 101); keypad_sample(-1, 125);
    keypad_sample(-2, 126); keypad_sample(3, 127); keypad_sample(3, 160);
    assert(!input_mode); /* Chords require release. */
    if (lcd_enabled) {
        /* High column inputs represent released keys in this register model. */
        test_extra_gpio[1].FIOPIN = KEYPAD_ACTIVE_HIGH ? 0 : 0xf0;
        ui_update(system_millis);
        for (t = 1; t < 1000; ++t) peripherals_tick((uint32_t)t);
        assert(lcd_boot == 8);
        assert(memcmp(lcd_sent, lcd_text[0], 16) == 0);
        assert(memcmp(lcd_sent + 16, lcd_text[1], 16) == 0);
        assert((test_extra_gpio[0].FIODIR & (1UL << 18)) != 0);
        /* Exercise the row scanner, rather than injecting a decoded key. */
        input_mode = 0; key_latched = 0; key_candidate = -1;
        for (t = 1000; t < 1040; ++t) {
            for (col = 0; col < 4; ++col) {
                pin = keypad_pins[col + 4];
                if ((key_row == 0 && col == 3) == KEYPAD_ACTIVE_HIGH)
                    io_ports[pin / 32]->FIOPIN |= 1UL << (pin % 32);
                else io_ports[pin / 32]->FIOPIN &= ~(1UL << (pin % 32));
            }
            keypad_step((uint32_t)t);
        }
        assert(input_mode == 1); /* Row 0, column 3 is A in the test map. */
    }
    io_group(conflict, 2, used); assert(peripheral_config_error);
    peripheral_config_error = 0;
    io_group(partial, 2, used); assert(peripheral_config_error);
    peripherals_init(); assert(!peripheral_config_error);
    puts("PASS peripheral configuration, debounce/release, LCD transport");
}

int main(void)
{
    test_complete_cycle();
    test_blink_and_wrap();
    test_gpio_timer_and_irq();
    test_late_poll_and_thresholds();
    test_emergency();
    test_long_buzzer();
    test_manual_and_ui();
    test_peripherals();
    test_boot_and_buzzer_output();
    printf("PASS polarity=%d: %lu phase/output checks; seven scenarios, "
           "countdowns, GPIO preservation, timer/IRQ, wrap and late polling\n",
           LED_ACTIVE_HIGH, checks);
    return 0;
}
