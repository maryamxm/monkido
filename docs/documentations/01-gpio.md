# 01 · GPIO: buttons and LEDs

Every digital pin on the ATmega328P belongs to a port (B, C or D) and is controlled by three 8-bit registers. Monkido uses port B for outputs (LEDs, buzzer) and port D for inputs (buttons).

## The three registers

| Register | Write 1 to bit n | Write 0 to bit n | Read |
|---|---|---|---|
| `DDRx` | pin n is an output | pin n is an input | current direction |
| `PORTx` | output: drive HIGH. Input: enable internal pull-up | output: drive LOW. Input: no pull-up (floating) | last value written |
| `PINx` | toggles `PORTx` bit n | no effect | the actual voltage level on the pin |

The rule people forget: on an input pin, `PORTx` does not output anything. It switches the internal pull-up resistor (20–50 kΩ to VCC, per the datasheet) on or off.

## Pin map in this project

| Uno pin | AVR pin | Role | Direction |
|---|---|---|---|
| D2–D5 | PD2–PD5 | Button0–3 | input, pull-up |
| D6 | PD6 | Start button | input, pull-up |
| D8 | PB0 | Buzzer | output |
| D9–D12 | PB1–PB4 | LED0–3 | output |
| A4, A5 | PC4, PC5 | I2C SDA, SCL | handed to the TWI peripheral (see [04](04-i2c-and-oled.md)) |

The Uno's "D9" is just a silkscreen label. In bare-metal code you always work with the port and bit, which is why `main.c` has `led_bits[] = { PB1, PB2, PB3, PB4 }` and `button_bits[] = { PD2, PD3, PD4, PD5 }`.

## Setup: `io_init()`

```c
DDRB |= (1 << BUZZER_BIT) | (1 << PB1) | (1 << PB2) | (1 << PB3) | (1 << PB4);  // outputs

DDRD &= ~((1 << PD2) | (1 << PD3) | (1 << PD4) | (1 << PD5));   // inputs
PORTD |=  (1 << PD2) | (1 << PD3) | (1 << PD4) | (1 << PD5);    // pull-ups on
```

The three bit idioms used everywhere:

```c
REG |=  (1 << n);    // set bit n, leave the others alone
REG &= ~(1 << n);    // clear bit n
if (REG & (1 << n))  // test bit n
```

`PB1`, `PD2` and so on are just bit numbers (1, 2, ...) from `<avr/io.h>`.

## Buttons are active-low

Each button connects the pin to GND. With the pull-up on, the pin reads HIGH when released and LOW when pressed. No external resistor is needed. That is why every check in the code compares with 0:

```c
if ((PIND & (1 << bit)) == 0) {   // pressed (LOW)
```

## LEDs are active-high

Pin → 220 Ω → LED → GND, so writing 1 turns it on. `lightLedAndPlayTone()`:

```c
PORTB |=  (1 << led_bits[ledIndex]);   // on
playTone(...);
PORTB &= ~(1 << led_bits[ledIndex]);   // off
```

Current check: (5 V − ~2 V red LED) / 220 Ω ≈ 14 mA; blue and white (~3 V) get about 9 mA. That is under the 20 mA you should aim for per pin (40 mA is the absolute maximum).

## Debouncing: `readButtonsWithTimeout()`

A mechanical contact bounces for a few milliseconds, so one press can look like several. The code handles it like this:

```c
if ((PIND & (1 << bit)) == 0) {      // first LOW seen
    delay_ms(20);                    // let the bounce settle
    while ((PIND & (1 << bit)) == 0) // wait for release
        ;
    *buttonIndex = i;
    return true;
}
```

`wait_for_start_button_press()` does the same for PD6. This works in practice, but be aware of what it does not do:

- **No re-check after the 20 ms.** A single noise spike counts as a press. A stricter version confirms the pin is still LOW after the delay.
- **No release debounce.** It returns on the first HIGH after release. It gets away with this because the caller immediately plays a tone (about 550 ms in practice, see [02](02-timing-and-sound.md)) before reading again.
- **Holding a button freezes the game.** The release loop has no timeout, so the 3 s input timeout does not apply while a button is held.
- **Lowest index wins.** Buttons are scanned 0→3, so if two are pressed, the lower one is reported.

A stricter press check, if you want one:

```c
static bool pressed_stable(uint8_t bit)
{
    if (PIND & (1 << bit)) return false;      // not LOW
    delay_ms(20);
    return (PIND & (1 << bit)) == 0;          // still LOW → real press
}
```

## Read-modify-write is not atomic

`PORTB |= (1 << led_bits[i])` uses a runtime bit number, so the compiler emits three instructions: `in`, `ori`, `out` (visible in `Debug/*.lss`). With a constant bit it can use a single `sbi`, which the `-Os` build does for `PORTB |= (1 << BUZZER_BIT)`. Three instructions are fine here because no interrupt touches `PORTB`. If an ISR ever writes the same port, an interrupt between `in` and `out` loses the ISR's change. Either keep ISRs off ports that main code writes, or wrap the write in `ATOMIC_BLOCK` from `<util/atomic.h>`.

## Check yourself

1. Why does `PORTD |= (1 << PD2)` on an input pin matter?
2. What would `PIND & (1 << PD2)` read if you forgot the pull-up and left the button unpressed?
3. Writing `PINB = (1 << PB0);` does what?

<details><summary>Answers</summary>

1. It turns on the internal pull-up, so the released button reads a defined HIGH.
2. Undefined: the pin floats and picks up noise, so it can read either value.
3. Toggles PB0's output, in one instruction.
</details>
