#pragma once
// frob:waive PARSE002 reason="extern C guard is C++ syntax inside a C header -- tree-sitter-c \
// salvages around it and every declaration still parses"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// HD44780 20x4 LCD via PCF8574 I2C backpack (address 0x27).
// Call lcd_init() once after sei(). All functions block until TWI is idle.

void lcd_init(void);
void lcd_set_cursor(uint8_t col, uint8_t row);
void lcd_write_char(char c);
void lcd_write_str(const char* s);
void lcd_clear(void);
void lcd_backlight(uint8_t on);

#ifdef __cplusplus
}
#endif
