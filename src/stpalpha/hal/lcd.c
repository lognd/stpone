#include <stpalpha/hal/lcd.h>

#include <avr/io.h>
#include <util/delay.h>

/* PCF8574 I2C address (A2:A0 open = 0x27). */
#define PCF8574_ADDR 0x27

/* PCF8574 bit assignments for the HD44780 backpack. */
#define PCF_RS (1 << 0)
#define PCF_RW (1 << 1)
#define PCF_E (1 << 2)
#define PCF_BL (1 << 3) /* backlight, active-high */
#define PCF_D4 (1 << 4)
#define PCF_D5 (1 << 5)
#define PCF_D6 (1 << 6)
#define PCF_D7 (1 << 7)

/* TWI bit-rate: 100 kHz at F_CPU=16 MHz.
   TWBR = (F_CPU / SCL - 16) / (2 * prescaler) = (16e6/100e3 - 16)/2 = 72. */
#define TWI_BAUD 72

/* HD44780 commands. */
#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME 0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_FUNCTIONSET 0x20

#define LCD_ENTRY_INC 0x02
#define LCD_DISPLAY_ON 0x04
#define LCD_4BITMODE 0x00
#define LCD_2LINE 0x08
#define LCD_5x8DOTS 0x00

/* Row start addresses for 20x4 display. */
static const uint8_t kRowOffset[4] = {0x00, 0x40, 0x14, 0x54};

static uint8_t g_backlight = PCF_BL;

/* ------------------------------------------------------------------
 * TWI helpers -- blocking, no interrupt handler needed.
 * ------------------------------------------------------------------ */

static void twi_wait(void) {
    while (!(TWCR & (1 << TWINT))) {
    }
}

static void twi_start(void) {
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    twi_wait();
}

static void twi_stop(void) {
    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
}

static void twi_write(uint8_t byte) {
    TWDR = byte;
    TWCR = (1 << TWINT) | (1 << TWEN);
    twi_wait();
}

/* Send one byte to the PCF8574. */
static void pcf_write(uint8_t data) {
    twi_start();
    twi_write((PCF8574_ADDR << 1) | 0); /* SLA+W */
    twi_write(data);
    twi_stop();
}

/* Pulse the E pin with the data byte already on the bus. */
static void lcd_pulse_enable(uint8_t data) {
    pcf_write(data | PCF_E);
    _delay_us(1);
    pcf_write(data & ~PCF_E);
    _delay_us(50);
}

/* Write a 4-bit nibble (in the upper nibble of nibble_byte). */
static void lcd_write_nibble(uint8_t nibble_byte) {
    pcf_write(nibble_byte | g_backlight);
    lcd_pulse_enable(nibble_byte | g_backlight);
}

/* Send a full byte to the HD44780 as two 4-bit transfers.
   rs=0 for command, rs=PCF_RS for data. */
static void lcd_send(uint8_t value, uint8_t rs) {
    uint8_t hi = (value & 0xF0) | rs;
    uint8_t lo = ((value << 4) & 0xF0) | rs;
    lcd_write_nibble(hi);
    lcd_write_nibble(lo);
}

/* ------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------ */

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P4"
// frob:doc docs/spec/L5-component-design/SUB-02-firmware-lcd.md#comp-0201
void lcd_init(void) {
    /* Configure TWI: 100 kHz, prescaler = 1 (TWPS = 0). */
    TWSR = 0x00;
    TWBR = TWI_BAUD;

    /* HD44780 power-on init sequence (datasheet Figure 24, 4-bit path).
       The display starts in 8-bit mode; we coax it into 4-bit with
       three writes of 0x03 then one write of 0x02. */
    _delay_ms(50);
    lcd_write_nibble(0x30);
    _delay_ms(5);
    lcd_write_nibble(0x30);
    _delay_us(150);
    lcd_write_nibble(0x30);
    _delay_us(150);
    lcd_write_nibble(0x20); /* switch to 4-bit mode */
    _delay_us(150);

    /* Function set: 4-bit, 2-line (covers 4-row displays), 5x8 font. */
    lcd_send(LCD_FUNCTIONSET | LCD_4BITMODE | LCD_2LINE | LCD_5x8DOTS, 0);
    _delay_us(150);

    /* Display on, cursor off, blink off. */
    lcd_send(LCD_DISPLAYCONTROL | LCD_DISPLAY_ON, 0);
    _delay_us(150);

    lcd_clear();

    /* Entry mode: increment cursor, no display shift. */
    lcd_send(LCD_ENTRYMODESET | LCD_ENTRY_INC, 0);
    _delay_us(150);
}

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P4"
// frob:doc docs/spec/L5-component-design/SUB-02-firmware-lcd.md#comp-0202
void lcd_set_cursor(uint8_t col, uint8_t row) {
    if (row > 3)
        row = 3;
    lcd_send(0x80 | (col + kRowOffset[row]), 0);
}

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P4"
// frob:doc docs/spec/L5-component-design/SUB-02-firmware-lcd.md#comp-0202
void lcd_write_char(char c) {
    lcd_send((uint8_t)c, PCF_RS);
}

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P4"
// frob:doc docs/spec/L5-component-design/SUB-02-firmware-lcd.md#comp-0202
void lcd_write_str(const char* s) {
    while (*s) {
        lcd_send((uint8_t)*s++, PCF_RS);
    }
}

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P4"
// frob:doc docs/spec/L5-component-design/SUB-02-firmware-lcd.md#comp-0202
void lcd_clear(void) {
    lcd_send(LCD_CLEARDISPLAY, 0);
    _delay_ms(2);
}

// docs/runbooks/hardware-tests.md P4, simulator evidence tests/system/test_firmware.py"
// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P4"
// frob:doc docs/spec/L5-component-design/SUB-02-firmware-lcd.md#comp-0202
void lcd_backlight(uint8_t on) {
    g_backlight = on ? PCF_BL : 0;
    pcf_write(g_backlight);
}
