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

/* TWI bit-rate: about 10 kHz at F_CPU=16 MHz, prescaler 16. The bus has no external pull-ups,
   only the ATmega32U4's internal ones (about 20-50 kOhm), whose edges need many microseconds to
   rise into the wiring capacitance; a slow clock leaves them room.
   SCL = F_CPU / (16 + 2 * TWBR * prescaler) = 16e6 / (16 + 2*50*16) = 9.9 kHz. */
#define TWI_BAUD 50
#define TWI_PRESCALE_BITS (1 << TWPS1) /* TWPS = 2: prescaler 16 */

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

/* Longest wait for one TWI step (~7 ms, far above a 10 kHz byte (0.9 ms)): a stuck bus (SDA/SCL
 * held low by a loose cable or motor EMI) must never freeze the main loop. */
#define TWI_TIMEOUT 20000u

static bool g_fault;

static void twi_wait(void) {
    uint16_t budget = TWI_TIMEOUT;
    while (!(TWCR & (1 << TWINT))) {
        if (--budget == 0) {
            g_fault = true; /* abandon this and every later transfer until cleared */
            TWCR = 0;       /* release the bus */
            return;
        }
    }
}

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P4"
// frob:doc docs/spec/L5-component-design/SUB-02-firmware-lcd.md#comp-0202
bool lcd_faulted(void) {
    return g_fault;
}

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P4"
// frob:doc docs/spec/L5-component-design/SUB-02-firmware-lcd.md#comp-0202
void lcd_clear_fault(void) {
    g_fault = false;
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

/* Send n bytes to the PCF8574 in ONE transfer (start, address, bytes, stop): fewer bus
   transactions mean less time exposed to noise and a faster repaint at the slow bit rate. */
static void pcf_burst(const uint8_t* bytes, uint8_t n) {
    if (g_fault) {
        return;
    }
    twi_start();
    if (!g_fault) {
        twi_write((PCF8574_ADDR << 1) | 0); /* SLA+W */
    }
    for (uint8_t i = 0; i < n && !g_fault; ++i) {
        twi_write(bytes[i]);
    }
    if (!g_fault) {
        twi_stop();
    }
}

/* Write a 4-bit nibble (in the upper nibble of nibble_byte): data, then E high, then E low. At
   10 kHz each byte takes 0.9 ms, far above the HD44780's 450 ns minimum E pulse. */
static void lcd_write_nibble(uint8_t nibble_byte) {
    uint8_t d = nibble_byte | g_backlight;
    uint8_t seq[3] = {d, (uint8_t)(d | PCF_E), (uint8_t)(d & ~PCF_E)};
    pcf_burst(seq, 3);
    _delay_us(50);
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

/* HD44780 reset-by-instruction: the datasheet's software init (Figure 24, 4-bit path) works from
   ANY state, so it also resynchronises a panel whose nibble phase was lost to a glitch. The
   display starts in 8-bit mode; we coax it into 4-bit with three writes of 0x03 then 0x02.
   It is split in two halves (about 21 ms and 33 ms at 9.9 kHz) so a heal never holds the main
   loop for more than ~35 ms (DEC-009). */
static void hd44780_setup_a(void) {
    lcd_write_nibble(0x30);
    _delay_ms(5);
    lcd_write_nibble(0x30);
    _delay_us(150);
    lcd_write_nibble(0x30);
    _delay_us(150);
    lcd_write_nibble(0x20); /* switch to 4-bit mode */
    _delay_us(150);
}

static void hd44780_setup_b(void) {
    /* Function set: 4-bit, 2-line (covers 4-row displays), 5x8 font. */
    lcd_send(LCD_FUNCTIONSET | LCD_4BITMODE | LCD_2LINE | LCD_5x8DOTS, 0);
    _delay_us(150);

    /* Display on, cursor off, blink off. */
    lcd_send(LCD_DISPLAYCONTROL | LCD_DISPLAY_ON, 0);
    _delay_us(150);

    /* Entry mode: increment cursor, no display shift. */
    lcd_send(LCD_ENTRYMODESET | LCD_ENTRY_INC, 0);
    _delay_us(150);

    /* Return Home: undoes a display shift a glitch may have caused (0x18/0x1C) without clearing
       DDRAM, so a heal never flickers. It needs 1.52 ms. */
    lcd_send(LCD_RETURNHOME, 0);
    _delay_ms(2);
}

static void twi_configure(void) {
    /* Configure TWI: 9.9 kHz, prescaler = 16 (TWPS = 2). */
    TWSR = TWI_PRESCALE_BITS;
    TWBR = TWI_BAUD;
}

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P4"
// frob:doc docs/spec/L5-component-design/SUB-02-firmware-lcd.md#comp-0201
void lcd_init(void) {
    twi_configure();
    _delay_ms(50);
    hd44780_setup_a();
    hd44780_setup_b();
    lcd_clear();
}

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P4"
// frob:doc docs/spec/L5-component-design/SUB-02-firmware-lcd.md#comp-0201
void lcd_recover_begin(void) {
    g_fault = false;
    TWCR = 0; /* release the bus before reconfiguring it */
    twi_configure();
    hd44780_setup_a();
}

// frob:waive TEST001 reason="AVR-only symbol -- bench evidence docs/runbooks/hardware-tests.md P4"
// frob:doc docs/spec/L5-component-design/SUB-02-firmware-lcd.md#comp-0201
void lcd_recover_finish(void) {
    hd44780_setup_b(); /* no clear: the caller repaints every character, so nothing flickers */
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
    pcf_burst(&g_backlight, 1);
}
