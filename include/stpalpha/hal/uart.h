#pragma once
// frob:waive PARSE002 reason="extern C guard is C++ syntax inside a C header"
// USART1 host link (D0 RX / D1 TX, 115200 8N1): interrupt-fed RX line buffer,
// polled TX. Used by the calibration/debug protocol (docs/hardware/serial-protocol.md).

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Configure USART1 and enable the RX interrupt.
void uart_init(void);

// Copy one complete received line (terminator stripped, NUL-terminated) into
// buf of size cap. Returns false if no full line is waiting. Lines end at
// '\n' or '\r'. A line that arrives while one is still unread, and an
// over-long or errored line, is dropped whole and counted.
bool uart_read_line(char* buf, uint8_t cap);

// Number of lines dropped since the last call (each deserves an "ERR busy").
uint8_t uart_take_dropped(void);

// Send a NUL-terminated string followed by "\n" (blocking, polled).
void uart_write_line(const char* s);

#ifdef __cplusplus
}
#endif
