# 04 · I2C (TWI) and the OLED

The display is a 128x64 OLED with an SH1106 controller, connected over I2C. Atmel calls its I2C peripheral **TWI** (Two-Wire Interface). It is the largest part of `main.c`: the bus driver, the display protocol, a framebuffer and two fonts.

## I2C in one screen

- Two wires: **SDA** (data) and **SCL** (clock). Both are open-drain: devices can only pull them LOW, and pull-up resistors bring them back HIGH.
- The master (the AVR) drives the clock. Every transfer looks like this:

```
START | address(7 bits) + R/W | ACK | byte | ACK | byte | ACK | ... | STOP
```

- **START**: SDA falls while SCL is HIGH. **STOP**: SDA rises while SCL is HIGH.
- **ACK**: after every 8 bits, the receiver pulls SDA LOW for one clock to say "got it". No ACK (a NACK) means nobody answered.
- The OLED's 7-bit address is `0x3C`. Shifted left with R/W = 0 (write), the first byte on the wire is `0x78`. Many modules print "0x78" on the back, which is the same device.

## TWI registers

| Register | Role |
|---|---|
| `TWBR` | bit-rate divider |
| `TWSR` | status code (top 5 bits) + prescaler (bottom 2 bits) |
| `TWCR` | control: `TWINT` (operation done / start next), `TWSTA` (send START), `TWSTO` (send STOP), `TWEN` (enable) |
| `TWDR` | the byte to send or the byte received |

### Bit rate: `twi_init()`

```c
TWSR = 0x00;                                     // prescaler 1
TWBR = (uint8_t)((F_CPU / 400000UL - 16) / 2);   // = 12
```

```
SCL = F_CPU / (16 + 2 × TWBR × prescaler) = 16 MHz / (16 + 24) = 400 kHz
```

### The handshake: `twi_start()`, `twi_write()`, `twi_stop()`

```c
TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);   // request START
while (!(TWCR & (1 << TWINT))) {;}                  // wait until done

TWDR = data;
TWCR = (1 << TWINT) | (1 << TWEN);                  // send TWDR
while (!(TWCR & (1 << TWINT))) {;}

TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWSTO);   // request STOP
```

`TWINT` works backwards from what you might expect. **Writing 1** to it clears the flag and starts the operation. The hardware **sets** it again when the operation is finished.

## What the driver skips

**Status codes are never checked.** After each step, `TWSR & 0xF8` says what happened:

| Code | Meaning |
|---|---|
| `0x08` | START sent |
| `0x18` | address sent, ACK received |
| `0x20` | address sent, **NACK** (no device) |
| `0x28` | data sent, ACK received |

With the OLED unplugged, the address gets a NACK, `TWINT` still sets, and the code carries on writing into nothing. The game runs blind instead of reporting the fault. If something holds SDA low, `twi_start()` waits forever. A minimal check:

```c
static bool twi_write_ok(uint8_t data, uint8_t expect)
{
    TWDR = data;
    TWCR = (1 << TWINT) | (1 << TWEN);
    for (uint16_t n = 0; !(TWCR & (1 << TWINT)); n++)
        if (n == 0xFFFF) return false;             // bus stuck
    return (TWSR & 0xF8) == expect;                // e.g. 0x18 after the address
}
```

**`twi_stop()` doesn't wait for the STOP to finish.** The datasheet clears `TWSTO` once the STOP is on the bus. Adding `while (TWCR & (1 << TWSTO));` before the next `twi_start()` is the safe form.

**Pull-ups.** `io_init()` turns on the internal pull-ups on PC4/PC5, but at 20–50 kΩ they are too weak for a clean 400 kHz edge. It works because most OLED breakout boards already carry 4.7–10 kΩ pull-ups. On a bare bus, add external 4.7 kΩ resistors.

## Talking to the display

After the address byte, the controller expects a **control byte**:

- `0x00`: the following bytes are commands
- `0x40`: the following bytes are pixel data

`ssd1306_send_command()` sends `START, 0x78, 0x00, cmd, STOP`, one transaction per command. `ssd1306_send_data_start()` sends `START, 0x78, 0x40`, and then pixel bytes follow until `twi_stop()`.

### Pages: how pixels map to bytes

The 64 rows are split into **8 pages** of 8 rows. Each byte is one 8-pixel **vertical** strip, with the LSB at the top:

```
page 0: rows 0–7      byte[x] bit 0 = (x, 0) ... bit 7 = (x, 7)
page 1: rows 8–15
...
page 7: rows 56–63
```

`ssd1306_draw_pixel()` does exactly that maths on the 1024-byte RAM buffer:

```c
uint16_t index = x + (y / 8) * SSD1306_WIDTH;   // which byte
uint8_t  bit   = 1 << (y & 7);                  // which bit in it
```

Nothing is sent while drawing. `ssd1306_update_display()` pushes the whole buffer, page by page:

```c
ssd1306_send_command(0xB0 | page);   // select page
ssd1306_send_command(0x02);          // column low nibble = 2
ssd1306_send_command(0x10);          // column high nibble = 0
// then 128 data bytes
```

A full frame is about 1100 bytes, roughly 25 ms of bus time at 400 kHz. The CPU busy-waits for all of it.

### SH1106, not SSD1306

The functions are named `ssd1306_*`, but two details show the chip is an SH1106:

- **Column offset 2.** The SH1106 has 132 columns of RAM and the 128-pixel panel shows columns 2–129. An SSD1306 starts at column 0, so this code would shift its image 2 px right.
- **Page addressing per page.** The SH1106 has no horizontal auto-wrap, so the column has to be set again for each page, as `ssd1306_update_display()` does. `ssd1306_init()` also sends `0x20, 0x00` (horizontal addressing mode) and `0x8D, 0x14` (charge pump on). Those are SSD1306 commands, not SH1106 ones (the SH1106 controls its DC-DC converter with `0xAD`). They are leftovers from an SSD1306 init sequence. The display works with them in place, so they are clutter rather than a bug, but check the SH1106 datasheet before you reuse this init sequence.

Renaming the prefix to `oled_` or `sh1106_` would make the code honest.

## Fonts

**Digits** (`digitFont`, 10 × 7 bytes) are stored in flash with `PROGMEM` and read with `pgm_read_byte()`. **Letters** (`textFont`) are not, so they cost 120 bytes of RAM (see the `.map`). Each font row uses the low 5 bits, with bit 4 as the leftmost column:

```
'1' = 0x04 ..#..
      0x0C .##..
      0x04 ..#..   ... 0x0E .###.
```

`oled_draw_char5x7_scaled()` draws each set bit as a `scale × scale` block, plus one extra column to the right for a bold look. Only `A C E G H I M O R S T V`, space and `!` exist, which is exactly what "START", "GAME OVER!!", "SC" and "HI" need. Any other character is silently skipped, and lowercase is converted to uppercase first (so `"GAme OvEr!!"` shows as GAME OVER!!).

### A small centring bug

`oled_draw_text_center_scaled("GAme OvEr!!", 2)`: 11 characters × 12 px − 2 = 130 px, wider than the 128 px screen. `(128 − 130) / 2 = −1` lands in a `uint8_t` as 255, and the coordinates wrap, so the text starts at x = −1 and the leftmost pixel column of the G is cut off. Running the drawing code on a PC and dumping the buffer confirms it. Fixes: use `int16_t` for x and clamp, use scale 1 for long strings, or shorten the message.

## Check yourself

1. Which byte and bit hold pixel (10, 20)?
2. What does the first byte on the wire look like for a *read* from 0x3C?
3. Why would an SSD1306 module show a 2-pixel shift with this code?

<details><summary>Answers</summary>

1. Page 2 (20 / 8), byte index 10 + 2 × 128 = 266, bit 20 & 7 = 4.
2. `(0x3C << 1) | 1 = 0x79`.
3. `update_display()` starts writing at column 2. The SH1106 needs that offset; the SSD1306 does not.
</details>
