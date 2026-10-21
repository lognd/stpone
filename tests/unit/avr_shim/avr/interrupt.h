#pragma once
/* Host stand-in for <avr/interrupt.h>: an ISR is a plain function the test calls. */
#define ISR_NOBLOCK
#define ISR(vec, ...) void vec(void)
#define cli() ((void)0)
#define sei() ((void)0)
