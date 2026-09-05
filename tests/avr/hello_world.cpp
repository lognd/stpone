#include <avr/io.h>
#include <avr/sleep.h>

// USART1: hardware serial on Pro Micro pins 0 (RX=PD2) and 1 (TX=PD3).
// 115200 baud at 16 MHz with U2X1: UBRR1 = 16  (actual ~117647, <2% error)
static constexpr uint8_t kBaud = 16;

static void uart_init() {
    UBRR1H = 0;
    UBRR1L = kBaud;
    UCSR1A = _BV(U2X1);
    UCSR1B = _BV(TXEN1);
    UCSR1C = _BV(UCSZ11) | _BV(UCSZ10); // 8N1
}

static void uart_write(const char c) {
    while ((UCSR1A & _BV(UDRE1)) == 0) {
    }
    UDR1 = c;
}

static void uart_print(const char* s) {
    while (*s != '\0') {
        uart_write(*s++);
    }
}

int main() {
    uart_init();
    for (;;) {
        uart_print("Hello world from ATmega32U4!\n");
    }
}
