# 03 · UART: debug printing

Monkido sends a text log (`READY`, `START`, `LEVEL n`, the game-over summary) from the ATmega328P's USART0. On the Uno, its TX pin (PD1, D1) is wired to the USB-serial chip, so any serial monitor on the PC sees it.

## What goes over the wire

UART is asynchronous: there is no clock line. Both sides agree on the bit rate beforehand, and the receiver re-synchronises on every start bit. One `8N1` character looks like this:

```
idle  start  b0 b1 b2 b3 b4 b5 b6 b7  stop  idle
 1      0    ── 8 data bits, LSB first ──   1     1
```

That is 10 bits per character, so 9600 baud carries 960 characters per second, about 1.04 ms each.

## Setup: `usart_init()`

```c
#define BAUD 9600
#define UBRR_VALUE ((F_CPU / (16UL * BAUD)) - 1)

UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
UBRR0L = (uint8_t)(UBRR_VALUE & 0xFF);
UCSR0B = (1 << TXEN0);                    // transmitter on (no RX)
UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);   // 8 data bits, no parity, 1 stop
```

`UBRR0` divides the clock to make the bit rate. In normal (16x oversampling) mode:

```
UBRR  = F_CPU / (16 × BAUD) − 1 = 16 000 000 / 153 600 − 1 = 104 − 1 = 103   (integer division)
actual baud = F_CPU / (16 × (UBRR + 1)) = 9615 baud   → +0.16 % error
```

The receiver tolerates roughly ±2 %, so 9600 is safe. The macro truncates instead of rounding, though, and that matters at higher speeds:

| Target | UBRR from this macro | Actual | Error |
|---|---|---|---|
| 9600 | 103 | 9615 | +0.2 % ✓ |
| 57600 | 16 | 58824 | +2.1 % (marginal) |
| 115200 | 7 | 125000 | +8.5 % ✗ garbage |
| 115200 with `U2X0` (8x mode) | 16 | 117647 | +2.1 % (marginal) |

For anything above 9600, use `<util/setbaud.h>`. It rounds correctly, sets `USE_2X` when that is more accurate, and warns at compile time if the error is too large.

## Sending: `usart_send_char()`

```c
while (!(UCSR0A & (1 << UDRE0))) { ; }   // wait until the data register is empty
UDR0 = c;                                // hardware shifts it out
```

`UDRE0` means "UDR0 is free to take the next byte". It does not mean "transmission finished" (that is `TXC0`). Polling it is simple but **blocking**: `gameOver()` prints about 60 characters, so the CPU waits about 60 ms in this loop. An interrupt-driven TX ring buffer (`USART_UDRE_vect`) would remove the wait. Here it doesn't matter, because the game is idle at that point anyway.

## Two things to know about the print helpers

**`usart_print_number()` uses `itoa` on a `uint16_t`.**

```c
char buf[6];
itoa(num, buf, 10);
```

`itoa` takes a signed `int`. Any value ≥ 32768 prints as negative, and `"-32768"` plus the terminator is 7 bytes, one more than `buf` holds. It never happens here because only scores up to 49 are printed, but `utoa(num, buf, 10)` is the correct call.

**String literals live in RAM.** On AVR, flash and RAM are separate address spaces, and plain `const char *` strings are copied into RAM at boot. The `.map` file shows 119 bytes of string literals in `.data`. With `PSTR("...")` and a `usart_print_P()` that reads with `pgm_read_byte()`, they stay in flash. That is the same trick `digitFont` already uses (see [04](04-i2c-and-oled.md)).

## Hooking up `printf` (optional)

```c
static int uart_putchar(char c, FILE *s) { usart_send_char(c); return 0; }
static FILE uart_out = FDEV_SETUP_STREAM(uart_putchar, NULL, _FDEV_SETUP_WRITE);
// in main(): stdout = &uart_out;  then printf("LEVEL %u\r\n", gameIndex);
```

This is convenient, but avr-libc's `vfprintf` adds about 1.5 KB of flash.

## Reading the log

Open a serial monitor at 9600 baud, 8N1 (Arduino IDE, PuTTY, `screen /dev/ttyACM0 9600`). Keep the monitor closed while flashing over USB, since the bootloader uses the same port.

## Check yourself

1. How long does `"LEVEL 12\r\n"` take to send at 9600 baud?
2. Why might a 115200 setting show garbage with this macro?
3. Why are D0/D1 a bad choice for buttons on an Uno?

<details><summary>Answers</summary>

1. 10 characters × 10 bits / 9600 ≈ 10.4 ms.
2. Truncation gives UBRR 7, which is 125000 baud, 8.5 % fast.
3. They are wired to the USB-serial chip, which is used for uploads and the serial log.
</details>
