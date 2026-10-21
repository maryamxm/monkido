# 02 · Timing and sound

## The clock

The Uno runs the ATmega328P from a 16 MHz crystal, so one CPU cycle is 62.5 ns. Most AVR instructions take 1 cycle; branches, 16-bit adds and memory loads take 2.

```c
#define F_CPU 16000000UL
```

`F_CPU` does not set the clock. The fuses and the crystal do that. `F_CPU` only tells `<util/delay.h>` and your own maths what the clock is. If it is wrong, every delay, baud rate and timer calculation is wrong by the same ratio.

## `_delay_ms()`: busy-waiting

`_delay_ms(x)` is a loop the compiler sizes from `F_CPU` at compile time. It needs a constant argument and optimisation turned on. That is why `main.c` wraps it:

```c
static void delay_ms(uint16_t ms)
{
    while (ms--) {
        _delay_ms(1);     // constant → compiler can size the loop
    }
}
```

Busy-waiting blocks the CPU completely. Interrupts still run during it and make it slightly longer: the Timer1 ISR below costs about 50 cycles every 1 ms, roughly 0.3 %.

## Timer1 as a 1 ms tick: `timer1_init()`

A hardware timer counts clock ticks on its own while the CPU does other work. In CTC mode ("Clear Timer on Compare") it counts from 0 up to `OCR1A`, fires an interrupt, and restarts at 0.

```c
TCCR1B |= (1 << WGM12);                 // CTC mode, TOP = OCR1A
OCR1A   = 249;
TIMSK1 |= (1 << OCIE1A);                // interrupt on compare match A
TCCR1B |= (1 << CS11) | (1 << CS10);    // prescaler 64 → timer starts
```

The CTC frequency formula is worth memorising:

```
f = F_CPU / (prescaler × (OCR + 1)) = 16 000 000 / (64 × 250) = 1000 Hz
```

The ISR just counts:

```c
ISR(TIMER1_COMPA_vect) { g_millis++; }
```

and `millis()` reads it with interrupts off:

```c
cli();  t = g_millis;  sei();
```

That `cli()`/`sei()` matters. `g_millis` is 32 bits and the AVR is an 8-bit CPU, so the read takes four instructions. Without `cli()`, the ISR could fire halfway through and you would get two bytes of the old value and two of the new. One improvement: `sei()` turns interrupts on unconditionally, even if they were off before the call. `ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { t = g_millis; }` from `<util/atomic.h>` restores the previous state instead.

`readButtonsWithTimeout()` uses `millis() - start < timeout_ms`. Unsigned subtraction stays correct when the counter wraps (after about 49.7 days).

## How a square wave makes a tone

A passive buzzer is a membrane that moves when the voltage changes. Switch the pin HIGH for half a period and LOW for the other half, and it vibrates at that frequency:

```
period T = 1 / f        half period = T / 2
262 Hz → T = 3.82 ms → 1.91 ms HIGH, 1.91 ms LOW
```

`playTone()` does it in software:

```c
uint32_t periods         = duration_ms * freq / 1000;
uint32_t cycles_per_half = F_CPU / (2 * freq);

while (periods--) {
    PORTB |= (1 << BUZZER_BIT);
    for (i = 0; i < cycles_per_half; i++) _NOP();
    PORTB &= ~(1 << BUZZER_BIT);
    for (i = 0; i < cycles_per_half; i++) _NOP();
}
```

The maths assumes each loop iteration takes **one** cycle.

## Why the tones are about 11x off

`i` is a `uint32_t`, so each iteration has to increment and compare four bytes. From `Debug/mini-project.lss`:

```
nop                    1 cycle
adiw r24, 1            2      ┐ i++ (32-bit)
adc  r26, r1           1      │
adc  r27, r1           1      ┘
cp/cpc/cpc/cpc         4        i < cycles_per_half (32-bit)
brcs back              2        taken branch
                      ──
                      11 cycles per iteration
```

So each half period is 11 times longer than intended. Pitch drops by 11x, and because `periods` is still computed for the intended pitch, duration grows by 11x:

| Button | Intended | Actually plays | Intended length | Actual length |
|---|---|---|---|---|
| 0 | B6, 1976 Hz | ~180 Hz | 50 ms | ~547 ms |
| 1 | C4, 262 Hz | ~24 Hz (a buzz of clicks) | 50 ms | ~547 ms |
| 2 | D8, 4699 Hz | ~427 Hz | 50 ms | ~547 ms |
| 3 | G5, 784 Hz | ~71 Hz | 50 ms | ~547 ms |

(Computed from the cycle count above plus the few cycles of loop overhead per half.)

The lesson is general: **cycle-counting loops in C depend on the compiler.** Rebuilding `main.c` with `-Os` (avr-gcc 7.3) gives a different loop: 11 cycles on the HIGH half and 12 on the LOW half. Change the optimisation level or the compiler version and the pitch changes.

Once the pitch is fixed, the tones will be the 50 ms the code asks for, which is very short. The ~550 ms you hear now is an accident, so you will probably want to raise the duration to 200–300 ms.

## The fix: let a timer make the wave

Option A (no rewiring). Timer1 is taken by `millis()`, so use Timer2 in CTC mode and toggle PB0 in its interrupt. Compiled and checked with avr-gcc for the ATmega328P:

```c
ISR(TIMER2_COMPA_vect)
{
    PINB = (1 << BUZZER_BIT);   // writing 1 to PINx toggles the pin
}

static void tone_start(uint16_t freq)
{
    static const uint16_t div[] = { 1, 8, 32, 64, 128, 256, 1024 };  // Timer2 prescalers
    for (uint8_t i = 0; i < 7; i++) {
        uint32_t ocr = F_CPU / (2UL * div[i] * freq) - 1;   // toggle at 2 * freq
        if (ocr <= 255) {                                   // Timer2 is 8-bit
            TCCR2A = (1 << WGM21);                          // CTC
            OCR2A  = (uint8_t)ocr;
            TCNT2  = 0;
            TCCR2B = i + 1;                                 // CS22:0 = 1..7
            TIMSK2 |= (1 << OCIE2A);
            return;
        }
    }
}

static void tone_stop(void)
{
    TCCR2B = 0;
    TIMSK2 &= ~(1 << OCIE2A);
    PORTB  &= ~(1 << BUZZER_BIT);
}

static void playTone(uint16_t freq, uint16_t duration_ms)
{
    if (freq == 0) return;
    tone_start(freq);
    uint32_t start = millis();
    while (millis() - start < duration_ms) { ; }
    tone_stop();
}
```

Resulting accuracy:

| Note | Prescaler | OCR2A | Actual |
|---|---|---|---|
| C4 262 Hz | 128 | 237 | 262.6 Hz |
| G5 784 Hz | 64 | 158 | 786.2 Hz |
| B6 1976 Hz | 32 | 125 | 1984.1 Hz |
| D8 4699 Hz | 8 | 211 | 4717.0 Hz |

The pitch no longer depends on the compiler, and the duration comes from `millis()`.

Option B (zero CPU, needs one wire moved). A timer can toggle a pin in hardware with no ISR, but only its own output-compare pin. PB0 has none. Timer2's OC2A is PB3 (D11, currently LED2). Swap the buzzer and LED2, set `COM2A0` in `TCCR2A`, and the timer drives the buzzer by itself. This is how Arduino's `tone()` and most PWM audio work.

## Check yourself

1. With prescaler 64, what `OCR1A` gives a 500 µs tick?
2. Why does making `i` a `uint8_t` not fix `playTone()` properly?
3. Why read `g_millis` with interrupts off?

<details><summary>Answers</summary>

1. 16 MHz / 64 = 250 kHz, so 125 counts: `OCR1A = 124`.
2. It cuts the cycles per iteration but not to 1, so the pitch is still wrong and still depends on the compiler. Worse, `i` can never reach a `cycles_per_half` above 255, and every note here needs more, so the loop never ends.
3. It is 4 bytes and the CPU reads 1 byte at a time; the ISR could change it mid-read.
</details>
