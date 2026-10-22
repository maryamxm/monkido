# 02 · Timer1

The timer registers in an AVR are simple counters. Their advantage is that the input clock and operation of the timer are independent of the program execution. The deterministic clock makes it possible to measure time by counting the elapsed cycles and taking the input frequency of the timer into account. 

The ATmega328P has two 8-bit (Timer0 & Timer2) and a 16-bit timer (Timer1).  
Below is a list of all the registers in Timer1 and what they do. 

| Registers     | What they do                                                 |
|---------------|--------------------------------------------------------------|
| TCCR1 (A/B/C) | Timer/counter control register, selects the mode of the timer (prescalar, outputs, etc.)  |
| TCNT1         | Timer/counter, sets the counter value itself                 |
| OCR1 (A/B)    | Output compare register, compares the counter values, if matches, a flag is set (OCF1) |
| OCF1 (A/B)    | Output compare match flag                                    |
| TIMSK1        | Timer interrupt mask register, enables interrupts for overflow or compare match |
| TIFR1         | Timer interrupt flag register                                |
| ICR1          | Input capture register                                       |

The timer registers can be configured in three modes:
* Normal mode: it increments with each clock.
* Time delay: measures how long it takes for the timer to overflow or reach a specific value.
* CTC (Clear Timer on Compare match) mode: it increments with the clock and counts until TCNT matches OCR. The timer is then cleared and the OCF flag is set.

## In this project

In this project, Timer1 is used. Below are the registers that are used to achieve a 1ms clock. 
The Uno runs the ATmega328P from a 16 MHz crystal and hence it is defined in the code as said. 

| Register | What is does                                                 |
|----------|--------------------------------------------------------------|
| TCCR1B   | Is set to CTC mode, selects the 64 prescaler and starts the timer |
| TCNT1    | The 16-bit counter. It goes up 1 every 4 µs (16 MHz ÷ 64)    |
| OCR1A    | Compare value and TOP. 250 counts × 4 µs = 1 ms per cycle    |
| TIMSK1   | Enables the compare-match A interrupt                        |