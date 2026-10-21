# Monkido learning notes

Short refreshers on the embedded concepts Monkido uses. Each one is tied to the functions in [`main.c`](../monkido/main.c) that use the concept. Read them in order; 01, 02 and 04 cover most of the code.

| # | Topic | Covers |
|---|---|---|
| [01](01-gpio.md) | GPIO | `DDRx`/`PORTx`/`PINx`, pull-ups, active-low buttons, debouncing |
| [02](02-timing-and-sound.md) | Timing and sound | clock, `_delay_ms`, Timer1 CTC tick, square-wave tones, why the tones are ~11x off, Timer2 fix |
| [03](03-uart.md) | UART | 8N1, baud rate and `UBRR`, debug printing |
| [04](04-i2c-and-oled.md) | I2C and the OLED | TWI registers, START/address/ACK, SH1106 pages, framebuffer, fonts |
| [05](05-adc-and-randomness.md) | ADC and randomness | ADC setup, floating-pin seed, why it is weak, better seeds |
| [06](06-eeprom.md) | EEPROM | high-score storage, wear, why `.eep` and ISP flashing reset it |
| [07](07-game-logic.md) | Game logic | the implicit state machine, the sequence array, the 49-step cap |
| [08](08-build-and-flash.md) | Build and flash | avr-gcc, `.hex`/`.elf`/`.map`/`.lss`, bootloader, avrdude, fuses |

Numbers (cycle counts, sizes, register values) come from the Debug build's `.lss` and `.map`, or from rebuilding with avr-gcc. Each file ends with a few self-check questions.
