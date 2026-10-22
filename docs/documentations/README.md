# Monkido learning notes

Short refreshers on the embedded concepts Monkido uses. Each one is tied to the functions in the code that use the concept. 

| # | Topic | Covers |
|---|---|---|
| [01](01-gpio.md) | GPIO | `DDRx`/`PORTx`/`PINx`, pull-ups, active-low buttons, debouncing |
| [02](02-timer1.md) | Timing and sound | clock, `_delay_ms`, Timer1 CTC tick, square-wave tones, why the tones are ~11x off, Timer2 fix |
| [03](03-uart.md) | USART | 8N1, baud rate and `UBRR`, debug printing |

Note: the documentation is not complete and it will be with the V2 of the game :)