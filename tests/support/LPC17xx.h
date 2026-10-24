/* Host register model ONLY. Never use this header in the Keil target. */
#ifndef TEST_LPC17XX_H
#define TEST_LPC17XX_H
#include <stdint.h>
typedef struct {
    volatile uint32_t PINSEL0, PINSEL1, PINSEL2, PINSEL3, PINSEL4,
        PINSEL5, PINSEL6, PINSEL7, PINSEL8, PINSEL9;
    volatile uint32_t PINMODE0, PINMODE1, PINMODE2, PINMODE3, PINMODE4,
        PINMODE5, PINMODE6, PINMODE7, PINMODE8, PINMODE9;
    volatile uint32_t PINMODE_OD0, PINMODE_OD1, PINMODE_OD2, PINMODE_OD3, PINMODE_OD4;
} TestPincon;
typedef struct {
    volatile uint32_t FIODIR, FIOMASK, FIOSET, FIOCLR, FIOPIN;
} TestGpio;
typedef TestGpio LPC_GPIO_TypeDef;
typedef struct {
    volatile uint32_t PCONP, PCLKSEL0;
} TestSc;
typedef struct {
    volatile uint32_t IR, TCR, CTCR, PR, MR0, MCR, CCR, EMR;
} TestTimer;
extern TestPincon test_pincon;
extern TestGpio test_gpio, test_extra_gpio[4];
extern TestSc test_sc;
extern TestTimer test_timer;
extern uint32_t SystemCoreClock;
#define LPC_PINCON (&test_pincon)
#define LPC_GPIO0 (&test_gpio)
#define LPC_GPIO1 (&test_extra_gpio[0])
#define LPC_GPIO2 (&test_extra_gpio[1])
#define LPC_GPIO3 (&test_extra_gpio[2])
#define LPC_GPIO4 (&test_extra_gpio[3])
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
