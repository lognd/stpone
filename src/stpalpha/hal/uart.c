/* frob:waive PARSE002 reason="ISR(vector) macro handlers trip the pure-Python tree-sitter path" */
/* USART1 host link; see include/stpalpha/hal/uart.h. */

#include <stpalpha/hal/uart.h>

#include <avr/interrupt.h>
#include <avr/io.h>

#define UART_UBRR 16u /* 115200 baud at 16 MHz with U2X1 (~117647, <2% error) */
#define RX_CAP 88u    /* longest accepted line is RX_CAP - 1 bytes */

static char g_rx[RX_CAP];
static volatile uint8_t g_rx_len;
static volatile bool g_rx_line_ready;
static volatile bool g_rx_overflow; /* discard bytes up to the next terminator */
static volatile bool g_rx_lost;     /* bytes of a line were discarded because one was pending */
static volatile uint8_t g_rx_dropped;

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P7"
// frob:doc docs/spec/L5-component-design/SUB-01-firmware-hal.md#comp-0106
void uart_init(void) {
    UBRR1H = 0;
    UBRR1L = UART_UBRR;
    UCSR1A = _BV(U2X1);
    UCSR1B = _BV(RXEN1) | _BV(TXEN1) | _BV(RXCIE1);
    UCSR1C = _BV(UCSZ11) | _BV(UCSZ10); /* 8N1 */
    DDRD &= (uint8_t)~_BV(PD2);         /* RXD1 in, pulled up: a floating line stays idle-high */
    PORTD |= _BV(PD2);
}

/* Assemble bytes into one line ('\n' or '\r' ends it; an empty line is ignored, so "\r\n"
 * is one terminator). While a finished line is still unread, a further line is discarded
 * WHOLE and counted (the host gets "ERR busy"); the pending line is never disturbed. */
ISR(USART1_RX_vect) {
    uint8_t status = UCSR1A;
    char c = (char)UDR1;
    if (status & (_BV(FE1) | _BV(DOR1))) {
        /* framing/overrun: the line is garbage up to its terminator */
        if (g_rx_line_ready) {
            g_rx_lost = true;
        } else {
            g_rx_overflow = true;
        }
        return;
    }
    if (c == '\n' || c == '\r') {
        if (g_rx_line_ready) {
            if (g_rx_lost) {
                g_rx_dropped++;
                g_rx_lost = false;
            }
        } else if (g_rx_overflow) {
            g_rx_overflow = false;
            g_rx_len = 0;
            g_rx_dropped++;
        } else if (g_rx_len > 0) {
            g_rx[g_rx_len] = '\0';
            g_rx_line_ready = true;
        }
        return;
    }
    if (g_rx_line_ready) {
        g_rx_lost = true;
        return;
    }
    if (g_rx_overflow) {
        return;
    }
    if (g_rx_len < RX_CAP - 1u) {
        g_rx[g_rx_len++] = c;
    } else {
        g_rx_overflow = true;
    }
}

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P7"
// frob:doc docs/spec/L5-component-design/SUB-01-firmware-hal.md#comp-0106
bool uart_read_line(char* buf, uint8_t cap) {
    if (!g_rx_line_ready) {
        return false;
    }
    uint8_t n = g_rx_len < (uint8_t)(cap - 1u) ? g_rx_len : (uint8_t)(cap - 1u);
    for (uint8_t i = 0; i < n; ++i) {
        buf[i] = g_rx[i];
    }
    buf[n] = '\0';
    cli();
    g_rx_len = 0;
    g_rx_line_ready = false;
    /* A line that was being discarded is still mid-flight: skip its remainder too. */
    if (g_rx_lost) {
        g_rx_overflow = true;
        g_rx_lost = false;
    }
    sei();
    return true;
}

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P7"
// frob:doc docs/spec/L5-component-design/SUB-01-firmware-hal.md#comp-0106
uint8_t uart_take_dropped(void) {
    cli();
    uint8_t n = g_rx_dropped;
    g_rx_dropped = 0;
    sei();
    return n;
}

static void put(char c) {
    while ((UCSR1A & _BV(UDRE1)) == 0) {
    }
    UDR1 = (uint8_t)c;
}

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P7"
// frob:doc docs/spec/L5-component-design/SUB-01-firmware-hal.md#comp-0106
void uart_write_line(const char* s) {
    while (*s) {
        put(*s++);
    }
    put('\n');
}
