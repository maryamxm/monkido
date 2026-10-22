# 03 · USART0 

UART (Universal Synchronous/Asynchronous Receiver-Transmitter) is a communication interface module used for serial communication protocols in the ATmega328P. The USART module allows the Atmega328P microcontroller to communicate with other devices, such as other microcontrollers, computers, or peripherals. The USART module in the Atmega328P microcontroller supports both synchronous (in master/slave set up) and asynchronous (commonly used) serial communication. 

Below are the register names and what they do.

| Registers | What they do                                                 |
|-----------|--------------------------------------------------------------|
| UDR0      | USART Data Register, acts as both the transmit and receive data buffer |
| UCSR0A    | USART Control and Status Register, holds status flags and control bits for serial communication  |
| UCSR0B    | USART Control and Status Register, enables TX & RX and their interrupts |
| UCSR0C    | USART Control and Status Register, sets the frame format (8N1 and so on) and mode |
| UBRR0H/L  | USART Baud Rate Register, decides the baud rate              |

## In this project 

Monkido sends a text log (`READY`, `START`, `LEVEL n`, the game-over summary) from the ATmega328P's USART0. On the Uno, its TX pin (PD1, D1) is wired to the USB-serial chip, so any serial monitor on the PC sees it.
UART is asynchronous: there is no clock line. Both sides agree on the bit rate beforehand, and the receiver resynchronises on every start bit. 
     
