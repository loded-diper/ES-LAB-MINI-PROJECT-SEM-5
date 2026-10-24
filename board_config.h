#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H
/* Set to 1 ONLY after installing and checking the combined wiring plan in
 * PERIPHERALS.md. The manual's standalone demo cables conflict with the LEDs.
 */
#ifndef BOARD_TRAINER_WIRING
#define BOARD_TRAINER_WIRING 0
#endif
#if BOARD_TRAINER_WIRING
#define BOARD_LCD_PINS {27,28,23,24,25,26}
#define BOARD_SEGMENT_PINS {15,16,17,18,19,20,21,22}
#define BOARD_DIGIT_PINS {52,53,60,61}
/* Provisional CNB3 order: MUST verify rows/columns on the trainer. */
#define BOARD_KEYPAD_PINS {74,75,76,77,55,56,57,58}
#define BOARD_BUZZER_PIN 121
#define BOARD_KEYMAP "0123456789ABCDEF"
#endif
/* Replace -1 ONLY with verified board pins, encoded as port * 32 + bit.
 * Example: P1.18 is BOARD_PIN(1,18). P0.4-P0.11 are reserved for LEDs.
 * All pins of an enabled peripheral must be assigned. See PERIPHERALS.md.
 */
#define BOARD_PIN(port, bit) ((port) * 32 + (bit))
#ifndef BOARD_LCD_PINS
#define BOARD_LCD_PINS {-1,-1,-1,-1,-1,-1} /* RS,E,D4,D5,D6,D7; RW grounded */
#endif
#ifndef BOARD_SEGMENT_PINS
#define BOARD_SEGMENT_PINS {-1,-1,-1,-1,-1,-1,-1,-1} /* a,b,c,d,e,f,g,dp */
#endif
#ifndef BOARD_DIGIT_PINS
#define BOARD_DIGIT_PINS {-1,-1,-1,-1} /* U7 BCD inputs A,B,C,D */
#endif
#ifndef BOARD_KEYPAD_PINS
#define BOARD_KEYPAD_PINS {-1,-1,-1,-1,-1,-1,-1,-1} /* R1..R4,C1..C4 */
#endif
#ifndef BOARD_BUZZER_PIN
#define BOARD_BUZZER_PIN -1
#endif
#ifndef SEGMENT_ACTIVE_HIGH
#define SEGMENT_ACTIVE_HIGH 1
#endif
/* Decoder codes for the two countdown positions; unused digits stay blank.
 * Verify physical left/right order; U7 outputs 0 and 1 are assumed here. */
#ifndef BOARD_DIGIT_CODES
#define BOARD_DIGIT_CODES {0,1}
#endif
#ifndef BOARD_KEYMAP
#define BOARD_KEYMAP "123A456B789C*0#D"
#endif
#ifndef KEYPAD_ACTIVE_HIGH
#define KEYPAD_ACTIVE_HIGH 1
#endif
#ifndef BUZZER_ACTIVE_HIGH
#define BUZZER_ACTIVE_HIGH 1
#endif
/* Active buzzer module only: 250ms on/off bursts, or continuous while green. */
#ifndef BUZZER_CONTINUOUS
#define BUZZER_CONTINUOUS 0
#endif
#endif
