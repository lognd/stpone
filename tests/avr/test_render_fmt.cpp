// On-target twin of tests/unit/render_driver.cpp: runs the shared render/host-link case table with
// avr-libc's printf and prints every line on USART1 (simavr echoes it; see
// tests/system/test_render_fmt.py). A screen that formats differently from glibc fails the test.
#include <avr/io.h>

#include "../unit/render_cases.h"

static void uart_init() {
    UBRR1H = 0;
    UBRR1L = 16; // 115200 baud at 16 MHz with U2X1
    UCSR1A = _BV(U2X1);
    UCSR1B = _BV(TXEN1);
    UCSR1C = _BV(UCSZ11) | _BV(UCSZ10);
}

static void uart_write(char c) {
    while ((UCSR1A & _BV(UDRE1)) == 0) {
    }
    UDR1 = static_cast<uint8_t>(c);
}

int main() {
    uart_init();
    render_cases::run([](const char* line) {
        while (*line != '\0') {
            uart_write(*line++);
        }
        uart_write('\n');
    });
    for (const char* t = "@done\n"; *t != '\0'; ++t) {
        uart_write(*t);
    }
    for (;;) {
    }
}
