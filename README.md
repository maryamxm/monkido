# Monkido

A Simon-style memory game for the ATmega328P (Arduino Uno), written in bare-metal C with no Arduino libraries. The board plays a growing sequence of LEDs and tones, and you repeat it on four buttons. Your score and an all-time high score show on a 128x64 I2C OLED; the high score is kept in EEPROM, so it survives power-off.

![Monkido on the breadboard](docs/images/board.jpg)

## Features

- Four LEDs and four buttons, each paired with its own tone on a passive buzzer
- 128x64 OLED (I2C) showing START, the live score, GAME OVER, and the high score
- High score saved in EEPROM
- 3-second timeout per button press
- Debug log over UART at 9600 baud (8N1)
- Register-level drivers for GPIO, Timer1, TWI (I2C), ADC, USART and EEPROM, plus a hand-written 5x7 font and framebuffer for the display

## Hardware

| Part | Qty | Pin (Uno) | AVR pin |
|---|---|---|---|
| Arduino Uno (ATmega328P, 16 MHz) | 1 | – | – |
| Push button, game (Button0–Button3) | 4 | D2, D3, D4, D5 | PD2–PD5 |
| Push button, start | 1 | D6 | PD6 |
| LED (LED0–LED3) + current-limiting resistor | 4 | D9, D10, D11, D12 | PB1–PB4 |
| Passive buzzer | 1 | D8 | PB0 |
| OLED 128x64, I2C, address 0x3C | 1 | A4 (SDA), A5 (SCL) | PC4, PC5 |
| Nothing connected (floating input, used as random seed) | – | A0 | ADC0 |

Wiring notes:

- Buttons connect between the pin and GND. The internal pull-ups are enabled, so a press reads LOW. No external resistors are needed.
- LEDs are active high: pin → resistor → LED anode, cathode → GND. LEDx lights for Buttonx.
- The buzzer must be **passive**. The firmware generates the square wave itself, so an active buzzer will not play the right tones.
- Leave A0 unconnected. The firmware reads its floating voltage once to seed the random generator.

![Schematic](docs/images/schematic.jpg)

## How to play

1. Power on. The OLED shows **HI** and the stored high score.
2. Press **START**. The screen shows `START`, then `SC 0`.
3. The board plays the sequence: each step lights one LED and plays its tone.
4. Repeat the sequence on the buttons. Each press lights its LED and plays its tone.
5. Get it right and the score goes up by one and the sequence grows by one step.
6. A wrong button, or no press within 3 seconds, ends the game. The OLED shows `GAME OVER!!`, then your score and the high score, then the high score alone. Press START to play again.

Your score is the number of rounds you completed. A new record is written to EEPROM immediately.

## Build and flash

Requirements: Atmel Studio 7 or Microchip Studio (Windows), and either an ISP programmer or the Uno's USB bootloader with `avrdude`.

1. Open `monkido.atsln` in Atmel Studio.
2. Pick the `Debug` (`-Og`) or `Release` (`-Os`) configuration and build (F7). The output is `monkido/<Configuration>/monkido.hex`.
3. Flash it.
   - With an ISP programmer (Atmel-ICE, USBasp and similar): Tools → Device Programming.
   - Over USB with the Uno bootloader:
     ```
     avrdude -c arduino -p m328p -P <COM_PORT> -b 115200 -U flash:w:monkido/Debug/monkido.hex:i
     ```
4. Optional: open a serial monitor at 9600 baud to see `READY`, `START`, `LEVEL n` and the game-over summary.

`F_CPU` is defined as 16 MHz in `main.c`. Change it if your board runs at a different clock.

Footprint (Debug build): about 4.0 KB of 32 KB flash, and 1340 of 2048 bytes of static RAM, most of it the 1 KB OLED framebuffer.

## Project structure

```
.
├── README.md
├── LICENSE
├── .gitignore
├── monkido.atsln          Atmel Studio solution
├── monkido/
│   ├── main.c             all firmware: drivers, display, game logic
│   ├── pitches.h          note name → frequency (Hz) table
│   └── monkido.cproj      Atmel Studio project
└── docs/images/           photos and schematic
```

## How it works

- **Timing.** Timer1 runs in CTC mode with prescaler 64 and `OCR1A = 249`, giving a 1 ms interrupt that drives `millis()`. This is used for the button timeout.
- **Sound.** `playTone()` bit-bangs PB0 high and low with a busy-wait loop. Tones are B6 (1976 Hz), C4 (262 Hz), D8 (4699 Hz) and G5 (784 Hz).
- **Display.** A 1 KB framebuffer in RAM is drawn into with a 5x7 font and then pushed to the OLED page by page over TWI at 400 kHz.
- **Randomness.** One ADC reading of the floating A0 pin seeds `rand()`.
- **Persistence.** The high score is one byte in EEPROM. `0xFF` (erased) is treated as 0, and writes use `eeprom_update_byte()` to avoid needless wear.
- **Control flow.** Everything runs in a single loop in `main()`: wait for START, extend the sequence, play it, read the input, then update the score or end the game.

## Known limitations

- **Tones are wrong by about 11x.** `playTone()` counts loop iterations as if each took one CPU cycle, but each iteration takes about 11 cycles (checked in the Debug `.lss`). Tones play roughly 11x lower in pitch and 11x longer than the code intends. The fix is to generate the tone in hardware with Timer2 in CTC toggle mode.
- **Blocking design.** Tones, delays and display updates all busy-wait, so nothing else can happen while they run, and buttons are read only while the game waits for input.
- **No I2C error handling.** The TWI driver never checks ACK/status codes. A missing or miswired OLED is not detected.
- **Display controller.** The code is named for the SSD1306, but it writes with a column offset of 2, which matches the SH1106 used on 1.3" modules. On a true SSD1306 (0.96") the image is shifted by 2 pixels.
- **Weak random seed.** A single reading of a floating pin can repeat between power-ups.
- **Length cap.** The sequence stops growing at 49 steps; after that the last step is replaced each round. Scores display up to 99.
- **Flashing the `.eep` file resets the high score** to the erased value (shown as 0).

## License

See [LICENSE](LICENSE).

## Author

Maryam
