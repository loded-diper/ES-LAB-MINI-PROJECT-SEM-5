/* Host register model ONLY. Never use this header in the Keil target. */
#ifndef TEST_LPC17XX_H
#define TEST_LPC17XX_H
#include <stdint.h>
typedef struct {
    volatile uint32_t PINSEL0, PINMODE0, PINMODE_OD0;
} TestPincon;
typedef struct {
    volatile uint32_t FIODIR, FIOMASK, FIOSET, FIOCLR;
} TestGpio;
typedef struct {
    volatile uint32_t PCONP, PCLKSEL0;
} TestSc;
typedef struct {
    volatile uint32_t IR, TCR, CTCR, PR, MR0, MCR, CCR, EMR;
} TestTimer;
extern TestPincon test_pincon;
extern TestGpio test_gpio;
extern TestSc test_sc;
extern TestTimer test_timer;
extern uint32_t SystemCoreClock;
#define LPC_PINCON (&test_pincon)
#define LPC_GPIO0 (&test_gpio)
#define LPC_SC (&test_sc)
#define LPC_TIM0 (&test_timer)
#define TIMER0_IRQn 1
void SystemCoreClockUpdate(void);
void NVIC_DisableIRQ(int irq);
void NVIC_ClearPendingIRQ(int irq);
void NVIC_SetPriority(int irq, uint32_t priority);
void NVIC_EnableIRQ(int irq);
void __WFI(void);
#endif
