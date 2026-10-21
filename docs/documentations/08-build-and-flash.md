# 08 · Build and flash

How `main.c` becomes bytes in the chip, what every file in `Debug/` is, and how to get it onto the board.

## The toolchain

Atmel Studio 7 is an IDE around the GNU AVR toolchain. The `.map` file shows this project was built with **avr-gcc 5.4.0**, avr-libc and the ATmega device pack 1.7.374. The steps:

```
main.c ──avr-gcc -c──► main.o ──avr-gcc (link)──► mini-project.elf ──avr-objcopy──► .hex  (flash image)
  + pitches.h                                         │                              .eep  (EEPROM image)
  + avr-libc headers                                  │                              .srec (flash, other format)
                                                      └──avr-objdump──► .lss (disassembly + source)
                                     linker also writes .map (where every symbol landed)
```

Key flags from `Debug/Makefile`:

| Flag | Why |
|---|---|
| `-mmcu=atmega328p` | target CPU: picks the instruction set, register addresses, memory sizes |
| `-Og` (Debug) / `-Os` (Release) | optimise for debugging / for size |
| `-ffunction-sections -fdata-sections` + `-Wl,--gc-sections` | put each function in its own section so the linker can drop unused ones |
| `-funsigned-char -fshort-enums -fpack-struct` | Atmel Studio defaults: `char` is unsigned, enums are 1 byte where possible |
| `-std=gnu99` | C99 plus GNU extensions (needed for `ISR()` and the like) |

`F_CPU` is not passed with `-D`. It is `#define`d at the top of `main.c`, which works because it comes before `#include <util/delay.h>`.

## What each output file is for

| File | Contents | You use it to |
|---|---|---|
| `.elf` | code, data, symbols, debug info | debug, inspect, or flash with tools that accept ELF |
| `.hex` | flash contents only, Intel HEX text | flash the chip (the usual choice) |
| `.eep` | EEPROM contents only | set initial EEPROM (here, resets the high score; see [06](06-eeprom.md)) |
| `.lss` | disassembly interleaved with C | check what the compiler really did (how the 11-cycle tone loop was found) |
| `.map` | address and size of every section and symbol | find where RAM and flash go |

### Reading a `.hex` line

```
:10 0000 00 0C945700 0C947400 0C947400 0C947400 BD
 │   │   │  └─ 16 data bytes                      └─ checksum
 │   │   └─ record type 00 = data
 │   └─ address 0x0000
 └─ byte count 0x10 = 16
```

The first four bytes, `0C 94 57 00`, are `jmp 0xAE`: the reset vector. The `.lss` confirms it, and `0xAE` is where the C runtime starts (it clears `r1`, sets up the stack, copies `.data`, zeroes `.bss`, then calls `main()`). The rest of the first 104 bytes are the other 25 interrupt vectors. Most point to `__bad_interrupt`; vector 11 (`TIMER1_COMPA`) points to your ISR.

## How big is it

From the `.map` (Debug build):

| | Bytes | Of | Notes |
|---|---|---|---|
| Flash (`.text` + `.data` init values) | 3824 + 260 = 4084 | 32 768 (32 256 usable with the bootloader) | ~12 % |
| RAM (`.data` + `.bss`) | 260 + 1080 = 1340 | 2048 | 1024 of it is `oled_buffer` |
| Free for stack | ~708 | | |

Rebuilt with avr-gcc 7.3, the same `main.c` comes out as: 3802 B of code at `-Og` and 3364 B at `-Os`. RAM is unchanged at both levels. Moving the strings and `textFont` to flash (see [03](03-uart.md), [04](04-i2c-and-oled.md)) would free about 240 B of RAM.

## Building without Atmel Studio

On Linux or macOS (`apt install gcc-avr avr-libc avrdude`, or on macOS `brew tap osx-cross/avr && brew install avr-gcc avrdude`), these commands build this project (checked with avr-gcc 7.3):

```sh
avr-gcc -mmcu=atmega328p -Os -Wall -std=gnu99 \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -o monkido.elf monkido/main.c
avr-objcopy -O ihex -R .eeprom monkido.elf monkido.hex
avr-size monkido.elf
```

## Getting it onto the chip

### Option 1: USB, through the bootloader

The Uno ships with **Optiboot**, a 512-byte bootloader in the top of flash. On reset it listens on the serial port for a moment. If avrdude talks to it, it writes the new program into flash; if not, it jumps to your code at address 0. Opening the port toggles DTR, which resets the board, so no button press is needed.

```sh
avrdude -c arduino -p m328p -P COM3 -b 115200 -U flash:w:mini-project.hex:i
#        protocol   chip     port   speed     write flash from Intel HEX file
```

(`/dev/ttyACM0` or `/dev/cu.usbmodem*` instead of `COM3` on Linux/macOS.) This does not erase EEPROM, so the high score survives. It cannot change fuses.

### Option 2: ISP programmer

An ISP programmer (Atmel-ICE, USBasp, another Arduino running "ArduinoISP") uses the 6-pin ICSP header: SPI plus RESET. It can write flash, EEPROM and fuses, with no bootloader needed.

```sh
avrdude -c usbasp -p m328p -U flash:w:mini-project.hex:i
```

Two side effects to know about:

- **It erases the bootloader.** A chip erase wipes all of flash. Your program still runs, but USB uploads stop working until you burn the bootloader again (Arduino IDE → Burn Bootloader).
- **It erases EEPROM** (the high score) unless `EESAVE` is set (see [06](06-eeprom.md)).

## Fuses

Fuses are configuration bits outside flash, set only through ISP. **0 = programmed (on)**, which is the opposite of what you would guess. The Uno defaults:

| Fuse | Value | Means |
|---|---|---|
| `lfuse` | `0xFF` | external 16 MHz crystal, slow start-up, **no** `CKDIV8` (so the chip really runs at 16 MHz) |
| `hfuse` | `0xDE` | SPI programming on, 512 B boot section, `BOOTRST` on (start in bootloader), `EESAVE` off |
| `efuse` | `0xFD` | brown-out reset at 2.7 V (may read back as `0x05`; the unused bits vary) |

Read them before changing anything:

```sh
avrdude -c usbasp -p m328p -U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h
```

Ways to lock yourself out: selecting a clock source that isn't fitted (the chip has no clock until you supply one externally), programming `RSTDISBL` (turns reset into a GPIO, so ISP stops working), or unprogramming `SPIEN`. Use a fuse calculator (e.g. engbedded's AVR Fuse Calculator) and double-check before writing.

The connection to your code: if `CKDIV8` were programmed, the chip would run at 2 MHz while `F_CPU` says 16 MHz, so every delay, baud rate and tone would be 8x off.

## Check yourself

1. You flashed with a USBasp and now Arduino IDE uploads fail. Why?
2. Which file would you open to see how many bytes `oled_buffer` takes?
3. Why is `.eeprom` stripped from the `.hex`?

<details><summary>Answers</summary>

1. The ISP chip erase removed the bootloader. Burn it again.
2. The `.map` (`.bss.oled_buffer 0x400`).
3. Flash and EEPROM are separate memories programmed with separate commands, so each gets its own image.
</details>
