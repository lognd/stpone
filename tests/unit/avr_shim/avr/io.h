#pragma once
/* Host stand-in for <avr/io.h>: the registers src/stpalpha/hal/setup.c touches, as plain
 * variables, so tests/unit/encoder_model.c can run the REAL Timer1 encoder ISR on the host. */
#include <stdint.h>

static volatile uint8_t g_reg_pinb, g_reg_pinc, g_reg_pind, g_reg_pine, g_reg_portb, g_reg_portc,
    g_reg_portd, g_reg_porte, g_reg_portf, g_reg_ddrb, g_reg_ddrc, g_reg_ddrd, g_reg_dde,
    g_reg_ddrf, g_reg_misc, g_reg_sreg;
static volatile uint16_t g_reg_tcnt1, g_reg_ocr1a;

#define PINB g_reg_pinb
#define PINC g_reg_pinc
#define PIND g_reg_pind
#define PINE g_reg_pine
#define PORTB g_reg_portb
#define PORTC g_reg_portc
#define PORTD g_reg_portd
#define PORTE g_reg_porte
#define PORTF g_reg_portf
#define DDRB g_reg_ddrb
#define DDRC g_reg_ddrc
#define DDRD g_reg_ddrd
#define DDRE g_reg_dde
#define DDRF g_reg_ddrf
#define SREG g_reg_sreg
#define MCUCR g_reg_misc
#define USBCON g_reg_misc
#define TCCR0A g_reg_misc
#define TCCR0B g_reg_misc
#define OCR0A g_reg_misc
#define TIMSK0 g_reg_misc
#define TCCR1A g_reg_misc
#define TCCR1B g_reg_misc
#define TIMSK1 g_reg_misc
#define TCNT1 g_reg_tcnt1
#define OCR1A g_reg_ocr1a
#define IVCE 0
#define WGM01 1
#define CS01 1
#define CS00 0
#define OCIE0A 1
#define WGM12 3
#define CS10 0
#define OCIE1A 1
#define PB1 1
#define _BV(b) (1 << (b))
