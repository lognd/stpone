#pragma once
// frob:waive PARSE002 reason="extern C guard is C++ syntax inside a C header -- tree-sitter-c \
// salvages around it and every declaration still parses"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// HD44780 20x4 LCD via PCF8574 I2C backpack (address 0x27).
// Call lcd_init() once after sei(). Each TWI step is bounded (a stuck bus sets the fault flag instead of hanging).

void lcd_init(void);
// Re-run the HD44780 software init without clearing (heals a glitched panel); the caller then
// repaints. Two halves (~21 ms and ~33 ms) so the caller can run its loop in between.
void lcd_recover_begin(void);
void lcd_recover_finish(void);
void lcd_set_cursor(uint8_t col, uint8_t row);
void lcd_write_char(char c);
void lcd_write_str(const char* s);
void lcd_clear(void);
void lcd_backlight(uint8_t on);

// A TWI transfer timed out (stuck bus): every LCD call is a no-op until
// lcd_clear_fault(). The caller retries at its own pace; nothing here blocks forever.
bool lcd_faulted(void);
void lcd_clear_fault(void);

#ifdef __cplusplus
}
#endif
