# 05 · ADC and randomness

Monkido needs a different sequence each game. It gets a "random" seed by reading the voltage on an unconnected pin (A0) with the ADC, and passes it to `srand()`.

## The ADC in brief

The ATmega328P has one 10-bit successive-approximation ADC, multiplexed over pins ADC0–ADC7. A conversion returns 0–1023:

```
ADC = Vin × 1024 / Vref        (Vref = 5 V → about 4.9 mV per step)
```

## Setup: `adc_init()`

```c
ADMUX  = (1 << REFS0);                                       // Vref = AVcc, channel ADC0
ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);  // enable, prescaler 128
```

- `REFS1:0 = 01` selects AVcc (5 V on the Uno) as the reference. `MUX3:0 = 0000` selects ADC0.
- The ADC clock must be 50–200 kHz for full 10-bit accuracy. 16 MHz / 128 = 125 kHz, which is inside the range.
- One conversion takes 13 ADC clocks (104 µs). The first one after enabling takes 25 (200 µs).

## Reading: `adc_read0()`

```c
ADCSRA |= (1 << ADSC);              // start conversion
while (ADCSRA & (1 << ADSC)) { ; }  // hardware clears ADSC when done
return ADC;                         // 16-bit read of ADCL + ADCH
```

The `ADC` macro reads `ADCL` first, then `ADCH`. The order matters: reading `ADCL` locks the result until `ADCH` is read, so you never get halves of two different conversions.

## Seeding: `main()`

```c
uint16_t seed = adc_read0();
srand(seed);
...
gameSequence[gameIndex] = (uint8_t)(rand() % 4);
```

`rand()` is a pseudo-random generator: a formula that produces a fixed sequence of numbers from the seed. The same seed always gives the same game. The only real randomness in the system is that one ADC reading.

## Why this is a weak seed

- **A floating pin isn't random noise.** It drifts with stray capacitance, nearby traces, mains hum and your hand. Readings tend to sit in a narrow band and change slowly, so in practice you get far fewer than 1024 distinct seeds.
- **It's read once, at boot.** If two power-ups read the same value, the first game after each is identical. Later games in the same session differ only because the `rand()` stream keeps going.
- **The best source is ignored.** The time a human takes to press START varies by milliseconds, while the timer counts in microseconds. That jitter is far more random than A0.

Easy experiment: add `usart_print_number(seed);` after `adc_read0()` and power-cycle the board ten times. If you see repeats or a tight cluster, you've measured the weakness.

## A better seed

Mix in the timer at the moment START is pressed. Timer1 counts at 250 kHz (4 µs per tick), so the low bits of `TCNT1` are effectively random to a human:

```c
wait_for_start_button_press();
srand(seed ^ TCNT1 ^ (uint16_t)millis());   // re-seed every game
```

If you want something more from the ADC, collect the least significant bit of many readings, because the LSB is the noisiest:

```c
uint16_t s = 0;
for (uint8_t i = 0; i < 16; i++)
    s = (s << 1) | (adc_read0() & 1);
```

## Modulo bias? Not here

`rand() % n` is biased when `RAND_MAX + 1` is not a multiple of `n`. avr-libc's `RAND_MAX` is 32767, so there are 32768 possible values, which is divisible by 4. Each colour is exactly equally likely.

Never use `rand()` for anything security-related. For a game it is fine.

## Check yourself

1. What does the ADC return for 2.5 V with a 5 V reference?
2. Why is a prescaler of 64 (250 kHz) worse than 128 here?
3. Why does re-seeding at the START press make sequences less predictable?

<details><summary>Answers</summary>

1. 2.5 × 1024 / 5 = 512.
2. 250 kHz is above the 200 kHz limit for full 10-bit accuracy. (Accuracy doesn't matter for a seed, but it does for real measurements.)
3. Press timing varies by far more than one 4 µs timer tick, so `TCNT1` at that moment is close to uniformly random.
</details>
