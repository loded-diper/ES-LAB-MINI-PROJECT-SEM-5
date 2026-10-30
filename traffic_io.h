#ifndef TRAFFIC_IO_H
#define TRAFFIC_IO_H
#include "board_config.h"
/* Header implementation keeps the Keil application a single translation unit. */
static const int lcd_pins[6] = BOARD_LCD_PINS;
static const int segment_pins[8] = BOARD_SEGMENT_PINS;
static const int digit_pins[4] = BOARD_DIGIT_PINS;
static const int keypad_pins[8] = BOARD_KEYPAD_PINS;
static const int buzzer_pin[1] = {BOARD_BUZZER_PIN};
static LPC_GPIO_TypeDef * const io_ports[5] = {
    LPC_GPIO0, LPC_GPIO1, LPC_GPIO2, LPC_GPIO3, LPC_GPIO4
};
static volatile uint32_t * const io_select[10] = {
    &LPC_PINCON->PINSEL0, &LPC_PINCON->PINSEL1,
    &LPC_PINCON->PINSEL2, &LPC_PINCON->PINSEL3,
    &LPC_PINCON->PINSEL4, &LPC_PINCON->PINSEL5,
    &LPC_PINCON->PINSEL6, &LPC_PINCON->PINSEL7,
    &LPC_PINCON->PINSEL8, &LPC_PINCON->PINSEL9
};
static volatile uint32_t * const io_mode[10] = {
    &LPC_PINCON->PINMODE0, &LPC_PINCON->PINMODE1,
    &LPC_PINCON->PINMODE2, &LPC_PINCON->PINMODE3,
    &LPC_PINCON->PINMODE4, &LPC_PINCON->PINMODE5,
    &LPC_PINCON->PINMODE6, &LPC_PINCON->PINMODE7,
    &LPC_PINCON->PINMODE8, &LPC_PINCON->PINMODE9
};
static volatile uint32_t * const io_od[5] = {
    &LPC_PINCON->PINMODE_OD0, &LPC_PINCON->PINMODE_OD1,
    &LPC_PINCON->PINMODE_OD2, &LPC_PINCON->PINMODE_OD3,
    &LPC_PINCON->PINMODE_OD4
};
volatile int peripheral_config_error = 0;
static int lcd_enabled, segment_enabled, keypad_enabled, buzzer_enabled;
static char lcd_text[2][17];
static int segment_value;
static int buzzer_on;
static uint32_t io_last_ms;

static int io_pin_valid(int pin)
{
    int port, bit;
    if (pin < 0 || pin >= 160) return 0;
    port = pin / 32; bit = pin % 32;
    if (port == 0) return bit <= 30 && bit != 12 && bit != 13 &&
                            !(bit >= 4 && bit <= 11);
    if (port == 1) return bit <= 31 && bit != 2 && bit != 3 &&
                            bit != 5 && bit != 6 && bit != 7 &&
                            bit != 11 && bit != 12 && bit != 13;
    if (port == 2) return bit <= 13;
    return (port == 3 && (bit == 25 || bit == 26)) ||
           (port == 4 && (bit == 28 || bit == 29));
}

/* Optional host observer records requested output levels, not hardware reads. */
#ifndef IO_WRITE_OBSERVER
#define IO_WRITE_OBSERVER(pin, high) ((void)0)
#endif
static void io_write(int pin, int high)
{
    if (high) io_ports[pin / 32]->FIOSET = 1UL << (pin % 32);
    else io_ports[pin / 32]->FIOCLR = 1UL << (pin % 32);
    IO_WRITE_OBSERVER(pin, high);
}
static void io_direction(int pin, int output)
{
    uint32_t mask;
    mask = 1UL << (pin % 32);
    if (output) io_ports[pin / 32]->FIODIR |= mask;
    else io_ports[pin / 32]->FIODIR &= ~mask;
}
static void io_configure(int pin, int output, int initial)
{
    uint32_t mask;
    unsigned int shift;
    shift = (unsigned int)(pin % 16) * 2U;
    mask = 3UL << shift;
    *io_select[pin / 16] &= ~mask;
    /* Trainer columns have pull-downs; match the selected scan polarity. */
    *io_mode[pin / 16] = (*io_mode[pin / 16] & ~mask) |
                           ((output ? 2UL : (KEYPAD_ACTIVE_HIGH ? 3UL : 0UL)) << shift);
    *io_od[pin / 32] &= ~(1UL << (pin % 32));
    io_ports[pin / 32]->FIOMASK &= ~(1UL << (pin % 32));
    io_write(pin, initial);
    io_direction(pin, output);
}
/* Reject partial, overlapping, reserved or nonexistent pin assignments before
 * touching any peripheral pins. A wholly unassigned group is disabled. */
static int io_group(const int *pins, int count, uint32_t used[5])
{
    int i, assigned;
    uint32_t mask;
    assigned = 0;
    for (i = 0; i < count; ++i) {
        if (pins[i] == -1) continue;
        ++assigned;
        if (!io_pin_valid(pins[i])) { peripheral_config_error = 1; continue; }
        mask = 1UL << (pins[i] % 32);
        if (used[pins[i] / 32] & mask) peripheral_config_error = 1;
        used[pins[i] / 32] |= mask;
    }
    if (assigned && assigned != count) peripheral_config_error = 1;
    return assigned == count;
}

static unsigned char seg_pattern(int value)
{
    static const unsigned char digits[10] = {
        0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f
    };
    return value < 0 ? 0x40 : digits[value % 10];
}
static void digit_select(int code)
{
    int i;
    for (i = 0; i < 4; ++i) io_write(digit_pins[i], (code >> i) & 1);
}
static void segment_step(void)
{
    static const int codes[2] = BOARD_DIGIT_CODES;
    static int digit;
    unsigned char pattern;
    int i;
    /* Blank ALL segments before changing the BCD selector. This avoids
     * transient wrong-digit illumination while four selector pins change. */
    for (i = 0; i < 8; ++i) io_write(segment_pins[i], !SEGMENT_ACTIVE_HIGH);
    digit_select(codes[digit]);
    pattern = seg_pattern(segment_value < 0 ? -1 :
                           (digit == 0 ? segment_value / 10 : segment_value));
    for (i = 0; i < 8; ++i)
        io_write(segment_pins[i], ((pattern >> i) & 1) == SEGMENT_ACTIVE_HIGH);
    digit ^= 1;
}

/* One active row; other rows are inputs. Read after 1ms settling. The ALS
 * matrix uses active-high rows and pulled-down columns (manual page 35). */
static int key_row, key_count, key_index, key_candidate, key_latched;
static uint32_t key_changed;
static void traffic_key(char key);
static void keypad_sample(int sample, uint32_t now)
{
    static const char keys[] = BOARD_KEYMAP;
    if (sample == -2) key_latched = 1;
    if (sample != key_candidate) { key_candidate = sample; key_changed = now; }
    if ((uint32_t)(now - key_changed) < 24UL) return;
    if (sample == -1) key_latched = 0;
    else if (!key_latched) {
        key_latched = 1;
        if (sample >= 0 && sample < 16) traffic_key(keys[sample]);
    }
}
static void keypad_step(uint32_t now)
{
    int col;
    for (col = 0; col < 4; ++col) {
        if (((io_ports[keypad_pins[col + 4] / 32]->FIOPIN >>
              (keypad_pins[col + 4] % 32)) & 1UL) == KEYPAD_ACTIVE_HIGH) {
            ++key_count; key_index = key_row * 4 + col;
        }
    }
    io_direction(keypad_pins[key_row], 0);
    key_row = (key_row + 1) % 4;
    if (key_row == 0) {
        keypad_sample(key_count == 0 ? -1 : (key_count == 1 ? key_index : -2), now);
        key_count = 0;
    }
    io_direction(keypad_pins[key_row], 1);
}

/* HD44780 16x2, write-only 4-bit interface. Each nibble has separate setup,
 * E-high and E-low ticks; even a slow foreground loop meets minimum timings.
 * Boot waits 50ms, then sends 3,3,3,2 with >=5ms spacing. */
static int lcd_boot, lcd_stage, lcd_pos, lcd_pending, lcd_half, lcd_rs;
static unsigned char lcd_byte;
static char lcd_sent[32];
static uint32_t lcd_wait_started, lcd_wait_ms;
static void lcd_step(uint32_t now)
{
    static const unsigned char init[] = {0x28,0x0c,0x06,0x01};
    int i, nibble, index;
    if ((uint32_t)(now - lcd_wait_started) < lcd_wait_ms) return;
    if (lcd_stage == 0) {
        if (lcd_boot < 4) { lcd_byte = lcd_boot == 3 ? 2 : 3; lcd_rs = 0; }
        else if (lcd_boot < 8) { lcd_byte = init[lcd_boot - 4]; lcd_rs = 0; }
        else if (lcd_pending) {
            lcd_byte = (unsigned char)lcd_text[lcd_pos / 16][lcd_pos % 16];
            lcd_rs = 1;
        } else {
            for (i = 0; i < 32; ++i) {
                index = (lcd_pos + i) % 32;
                if (lcd_sent[index] != lcd_text[index / 16][index % 16]) break;
            }
            if (i == 32) return;
            lcd_pos = index;
            lcd_byte = (unsigned char)(0x80 + (index >= 16 ? 0x40 : 0) + index % 16);
            lcd_rs = 0;
        }
        lcd_half = 0; lcd_stage = 1;
    }
    if (lcd_stage == 1) {
        nibble = lcd_boot < 4 || lcd_half ? lcd_byte & 15 : lcd_byte >> 4;
        io_write(lcd_pins[0], lcd_rs);
        for (i = 0; i < 4; ++i) io_write(lcd_pins[i + 2], (nibble >> i) & 1);
        lcd_stage = 2;
    } else if (lcd_stage == 2) {
        io_write(lcd_pins[1], 1); lcd_stage = 3;
    } else {
        io_write(lcd_pins[1], 0);
        if (lcd_boot >= 4 && !lcd_half) { lcd_half = 1; lcd_stage = 1; }
        else {
            lcd_stage = 0;
            if (lcd_boot < 8) ++lcd_boot;
            else if (lcd_rs) { lcd_sent[lcd_pos] = (char)lcd_byte; lcd_pending = 0; }
            else lcd_pending = 1;
        }
    }
    lcd_wait_started = now;
    lcd_wait_ms = lcd_stage == 0 ? 5UL : 1UL;
}
static void peripherals_init(void)
{
    uint32_t used[5] = {0,0,0,0,0};
    int i, segments, digits;
    peripheral_config_error = 0;
    lcd_enabled = io_group(lcd_pins, 6, used);
    segments = io_group(segment_pins, 8, used);
    digits = io_group(digit_pins, 4, used);
    if (segments != digits) peripheral_config_error = 1;
    segment_enabled = segments && digits;
    keypad_enabled = io_group(keypad_pins, 8, used);
    buzzer_enabled = io_group(buzzer_pin, 1, used);
    if (peripheral_config_error) {
        lcd_enabled = segment_enabled = keypad_enabled = buzzer_enabled = 0;
        return;
    }
    if (lcd_enabled) for (i = 0; i < 6; ++i) io_configure(lcd_pins[i], 1, 0);
    if (segment_enabled) {
        for (i = 0; i < 8; ++i) io_configure(segment_pins[i], 1, !SEGMENT_ACTIVE_HIGH);
        for (i = 0; i < 4; ++i) io_configure(digit_pins[i], 1, 1);
    }
    if (buzzer_enabled) io_configure(buzzer_pin[0], 1, !BUZZER_ACTIVE_HIGH);
    if (keypad_enabled) {
        for (i = 0; i < 8; ++i) io_configure(keypad_pins[i], 0, KEYPAD_ACTIVE_HIGH);
        io_direction(keypad_pins[0], 1);
    }
    key_row = key_count = key_latched = 0; key_candidate = -1;
    key_changed = system_millis;
    lcd_boot = lcd_stage = lcd_pending = lcd_pos = 0;
    for (i = 0; i < 32; ++i) lcd_sent[i] = '\0';
    lcd_wait_started = io_last_ms = system_millis; lcd_wait_ms = 50;
}
static void peripherals_tick(uint32_t now)
{
    if (now == io_last_ms) return;
    io_last_ms = now;
    if (keypad_enabled) keypad_step(now);
    if (segment_enabled) segment_step();
    if (lcd_enabled) lcd_step(now);
    if (buzzer_enabled) io_write(buzzer_pin[0], buzzer_on == BUZZER_ACTIVE_HIGH);
}
#endif
