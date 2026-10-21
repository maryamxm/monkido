# 01 · GPIO: buttons and LEDs

On an AVR, each I/O port is controlled by exactly three registers. Each port n the ATmega328p has three 8-bit registers, where bit n controls pin n of that port. 

## The three registers
| Register | Name                    | What it does                                                 |
|----------|-------------------------|--------------------------------------------------------------|
| DDRx     | Data Direction Register | 1 makes the pin an output, 0 makes it an input               |
| PORTx    | Data Register           | For outputs: 1 drives high, 0 drives low. For inputs: 1 enables the internal pull-up |
| PINx     | Input Pins Address      | Reading gives the actual logic level on each pin. On the ATmega328P, writing 1 to a bit toggles the PORTx bit |

Combining DDRx and PORTx gives four possible pin states. The input with pull-up state is the one you will use most with the switches, because it removes the need for an external resistor. 

| DDxn | PORTxn | Pin state                           |
|------|--------|-------------------------------------|
| 0    | 0      | Input, high impedance (floating)    |
| 0    | 1      | Input with internal pull-up enabled |
| 1    | 0      | Output, driven low                  |
| 1    | 1      | Output, driven high                 |

> An input pin with nothing connected and no pull-up floats. Reading it gives random values, and it can also increase current consumption. Always enable the pull-up or tie unused pins to a defined level.

Bellow are fundamental ways of writing modifying the registers:

```
REG |=  (1 << n);    // set bit n, leave the others alone
REG &= ~(1 << n);    // clear bit n
if (REG & (1 << n))  // test bit n
```

- - -

## In this project

The Arduino Uno pins are mapped to the registers in the table below, showing their role and state (PD0 is bit 0 of port D). 

| Uno pin | AVR pin   | Role         | State                         |
|---------|-----------|--------------|-------------------------------|
| D2-D5   | PD2-PD5   | Button 0-3   | Input, pull-up (active low)   |
| D6      | PD6       | Start button | Input, pull-up (active low)   |
| D8      | PB0       | Buzzer       | Output                        |
| D9–D12  | PB1–PB4   | LED 0–3      | Output                        |
| A4, A5  | PC4, PC5  | I2C SDA, SCL | Handed to the TWI peripheral  |
| A0      | PC0       | Floating     | Input                         |

Buttons are set to active low. It means that the pin is internally connected to the 5V and an internal resistance. When the button is pushed, the pin reads LOW and when it is released, it reads LOW.