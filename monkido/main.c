/* 
Arduino Memory Game
The game plays a sequence of LEDs and the player is expected to repeat the played sequence, each 
successful attempt adds an extra LED to the sequence. An OLED display is used to show the start
and game over of the game, the number of sequences which is played, and the highest score 
achieved and it is saved in the EEPROM to not loose the highest score on power off.

To use this code make the following connections between the hardware and the arduino:

D2  -- Button0
D3  -- Button1
D4  -- Button2
D5  -- Button3

D6  -- Start Button

D8  -- Passive Buzzer

D9  -- LED0
D10 -- LED1
D11 -- LED2
D12 -- LED3

A4  -- OLED SDA
A5  -- OLED SCL

A0  -- NO Connection (ADC0)

Caution: The game is simple and not attractive at first sight; as in practice, several players had suffered addiction
and mental fatigue due to repeated plays to beat the high score. 

Created by Maryam :)
 */

#define F_CPU 16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdlib.h>
#include <stdbool.h>
#include <avr/cpufunc.h>
#include <avr/pgmspace.h>
#include <string.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>   // EEPROM
#include "pitches.h"

#define BUZZER_BIT   PB0
#define START_BUTTON_BIT PD6
#define MAX_GAME_LENGTH 50
#define BAUD 9600
#define UBRR_VALUE ((F_CPU / (16UL * BAUD)) - 1)

static const uint8_t led_bits[4]    = { PB1, PB2, PB3, PB4 };
static const uint8_t button_bits[4] = { PD2, PD3, PD4, PD5 };

// Tones for each color
static const int gameTones[4] = { NOTE_B6, NOTE_C4, NOTE_D8, NOTE_G5 };

// Game state
static uint8_t gameSequence[MAX_GAME_LENGTH] = {0};
static uint8_t gameIndex  = 0;
static uint8_t highScore  = 0; 

// EEPROM-stored high score (persistent across power-off)
static uint8_t EEMEM ee_highScore_eep = 0xFF;   // 0xFF means "not initialized"

//////////////////////////////////
// Timer for time-out (millis)
//////////////////////////////////

volatile uint32_t g_millis = 0;

ISR(TIMER1_COMPA_vect)
{
	g_millis++;        // 1 ms tick
}

static void timer1_init(void)
{
	// CTC mode
	TCCR1B |= (1 << WGM12);

	// 16 MHz / 64 = 250 kHz -> 250 counts = 1 ms
	OCR1A   = 249;

	// Enable compare A interrupt
	TIMSK1 |= (1 << OCIE1A);

	// Start timer with prescaler 64
	TCCR1B |= (1 << CS11) | (1 << CS10);
}

static uint32_t millis(void)
{
	uint32_t t;
	cli();
	t = g_millis;
	sei();
	return t;
}

//////////////////////////////////
// I2C / SSD1306 OLED SUPPORT
//////////////////////////////////

#define SSD1306_I2C_ADDR   0x3C
#define SSD1306_WIDTH      128
#define SSD1306_HEIGHT     64
static uint8_t oled_buffer[SSD1306_WIDTH * SSD1306_HEIGHT / 8];

// TWI (I2C)
static void twi_init(void)
{
	// 400 kHz approx at 16 MHz
	TWSR = 0x00;
	TWBR = (uint8_t)((F_CPU / 400000UL - 16) / 2);
}

static void twi_start(void)
{
	TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
	while (!(TWCR & (1 << TWINT))) {;}
}

static void twi_stop(void)
{
	TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWSTO);
}

static void twi_write(uint8_t data)
{
	TWDR = data;
	TWCR = (1 << TWINT) | (1 << TWEN);
	while (!(TWCR & (1 << TWINT))) {;}
}

// SSD1306 basic ops
static void ssd1306_send_command(uint8_t cmd)
{
	twi_start();
	twi_write((SSD1306_I2C_ADDR << 1) | 0); // write
	twi_write(0x00);                        // control: command
	twi_write(cmd);
	twi_stop();
}

static void ssd1306_send_data_start(void)
{
	twi_start();
	twi_write((SSD1306_I2C_ADDR << 1) | 0); // write
	twi_write(0x40);                        // control: data
}

static void ssd1306_send_data_byte(uint8_t data)
{
	twi_write(data);
}

static void ssd1306_send_data_end(void)
{
	twi_stop();
}

static void ssd1306_clear_buffer(void)
{
	memset(oled_buffer, 0x00, sizeof(oled_buffer));
}

static void ssd1306_draw_pixel(uint8_t x, uint8_t y, bool color)
{
	if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) return;
	uint16_t index = x + (y / 8) * SSD1306_WIDTH;
	uint8_t  bit   = 1 << (y & 7);

	if (color) {
		oled_buffer[index] |= bit;
		} else {
		oled_buffer[index] &= ~bit;
	}
}

static void ssd1306_update_display(void)
{
	for (uint8_t page = 0; page < 8; page++) {
		
		ssd1306_send_command(0xB0 | page);

		// Set column address with offset 2
		ssd1306_send_command(0x02);  // lower column address
		ssd1306_send_command(0x10);  // higher column address

		ssd1306_send_data_start();

		uint16_t index = page * SSD1306_WIDTH;   // 128 bytes per page
		for (uint8_t x = 0; x < SSD1306_WIDTH; x++) {
			ssd1306_send_data_byte(oled_buffer[index++]);
		}

		ssd1306_send_data_end();
	}
}

static void ssd1306_init(void)
{
	_delay_ms(100);

	ssd1306_send_command(0xAE); // display off
	ssd1306_send_command(0xD5); // clock
	ssd1306_send_command(0x80);
	ssd1306_send_command(0xA8); // multiplex
	ssd1306_send_command(0x3F); // 63

	ssd1306_send_command(0xD3); // offset
	ssd1306_send_command(0x00);
	ssd1306_send_command(0x40); // start line

	ssd1306_send_command(0x8D); // charge pump
	ssd1306_send_command(0x14);

	ssd1306_send_command(0x20); // memory mode
	ssd1306_send_command(0x00); // horizontal

	ssd1306_send_command(0xA1); // seg remap
	ssd1306_send_command(0xC8); // com scan dec

	ssd1306_send_command(0xDA); // com pins
	ssd1306_send_command(0x12);

	ssd1306_send_command(0x81); // contrast
	ssd1306_send_command(0x7F);

	ssd1306_send_command(0xD9); // pre-charge
	ssd1306_send_command(0xF1);

	ssd1306_send_command(0xDB); // vcom detect
	ssd1306_send_command(0x40);

	ssd1306_send_command(0xA4); // resume
	ssd1306_send_command(0xA6); // normal
	ssd1306_send_command(0xAF); // display on

	ssd1306_clear_buffer();
	ssd1306_update_display();
}

// tiny 5x7 digit font (0–9) 
static const uint8_t digitFont[10][7] PROGMEM = {
	// each row: 5 LSB bits used (col 0..4)
	// 0
	{0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
	// 1
	{0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
	// 2
	{0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
	// 3
	{0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E},
	// 4
	{0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
	// 5
	{0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
	// 6
	{0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
	// 7
	{0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
	// 8
	{0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
	// 9
	{0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x1C}
};

static void oled_draw_digit_scaled(uint8_t digit, uint8_t x, uint8_t y, uint8_t scale)
{
	if (digit > 9 || scale == 0) return;

	for (uint8_t row = 0; row < 7; row++) {
		uint8_t rowBits = pgm_read_byte(&digitFont[digit][row]);
		for (uint8_t col = 0; col < 5; col++) {
			if (rowBits & (1 << (4 - col))) {
				for (uint8_t dy = 0; dy < scale; dy++) {
					for (uint8_t dx = 0; dx < scale; dx++) {
						ssd1306_draw_pixel(
						x + col * scale + dx,
						y + row * scale + dy,
						true
						);
					}
				}
			}
		}
	}
}

// 5x7 TEXT FONT FOR OLED MESSAGES
typedef struct {
	char c;
	uint8_t rows[7];
} FontChar;

// Simple 5x7 uppercase letters + space + '!'
static const FontChar textFont[] = {
	// A
	{ 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
	// C
	{ 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
	// E
	{ 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
	// G
	{ 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E } },
	// H
	{ 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
	// I
	{ 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
	// M
	{ 'M', { 0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11 } },
	// O
	{ 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
	// R
	{ 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
	// S
	{ 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
	// T
	{ 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
	// V
	{ 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
	// Space
	{ ' ', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	// !
	{ '!', { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 } },

	// terminator
	{ 0,   { 0, 0, 0, 0, 0, 0, 0 } }
};

// Draw a single 5x7 character scaled
static void oled_draw_char5x7_scaled(char ch, uint8_t x, uint8_t y, uint8_t scale)
{
	if (scale == 0) return;

	// convert lowercase to uppercase
	if (ch >= 'a' && ch <= 'z') {
		ch = ch - 'a' + 'A';
	}

	for (uint8_t i = 0; ; i++) {
		char fc = textFont[i].c;
		if (fc == 0) {
			return;
		}
		if (fc == ch) {
			for (uint8_t row = 0; row < 7; row++) {
				uint8_t rowBits = textFont[i].rows[row];
				for (uint8_t col = 0; col < 5; col++) {
					if (rowBits & (1 << (4 - col))) {

						// scaled "bold" block
						for (uint8_t dy = 0; dy < scale; dy++) {
							for (uint8_t dx = 0; dx < scale; dx++) {
								ssd1306_draw_pixel(
								x + col * scale + dx,
								y + row * scale + dy,
								true
								);
								// extra column to the right to make it a bit bolder
								ssd1306_draw_pixel(
								x + col * scale + dx + 1,
								y + row * scale + dy,
								true
								);
							}
						}

					}
				}
			}
			return;
		}
	}
}

// Draw a scaled string centered on the OLED
static void oled_draw_text_center_scaled(const char *s, uint8_t scale)
{
	uint8_t len = strlen(s);
	if (len == 0 || scale == 0) return;

	uint8_t charCoreW = 5 * scale;
	uint8_t gap       = scale;      
	uint8_t charW     = charCoreW + gap;
	uint8_t charH     = 7 * scale;

	uint8_t textW     = len * charW - gap;
	uint8_t textH     = charH;

	uint8_t startX = (SSD1306_WIDTH  - textW)  / 2;
	uint8_t startY = (SSD1306_HEIGHT - textH) / 2;

	uint8_t x = startX;
	for (uint8_t i = 0; i < len; i++) {
		oled_draw_char5x7_scaled(s[i], x, startY, scale);
		x += charW;
	}
}

static void oled_show_message(const char *s)
{
	uint8_t scale = 2;
	ssd1306_clear_buffer();
	oled_draw_text_center_scaled(s, scale);
	ssd1306_update_display();
}

//////////////////////////////////
// Score display
//////////////////////////////////

static void oled_draw_big_number(uint8_t value, uint8_t y, uint8_t scale)
{
	if (scale == 0) return;
	if (value > 99) value = 99; 

	uint8_t tens = value / 10;
	uint8_t ones = value % 10;
	uint8_t numDigits = (value >= 10) ? 2 : 1;

	uint8_t digitW = 5 * scale;
	uint8_t digitH = 7 * scale;
	(void)digitH;
	uint8_t gap    = 1 * scale;

	uint8_t totalWidth = numDigits * digitW + (numDigits - 1) * gap;
	uint8_t startX     = (SSD1306_WIDTH - totalWidth) / 2;

	if (numDigits == 2) {
		oled_draw_digit_scaled(tens, startX,              y, scale);
		oled_draw_digit_scaled(ones, startX + digitW + gap, y, scale);
		} else {
		oled_draw_digit_scaled(ones, startX,              y, scale);
	}
}

static void oled_draw_big_number_at(uint8_t value, uint8_t x, uint8_t y, uint8_t scale)
{
	if (scale == 0) return;
	if (value > 99) value = 99;

	uint8_t tens = value / 10;
	uint8_t ones = value % 10;
	uint8_t digitW = 5 * scale;
	uint8_t gap    = 1 * scale;

	if (value >= 10) {
		// two digits
		oled_draw_digit_scaled(tens, x,                 y, scale);
		oled_draw_digit_scaled(ones, x + digitW + gap,  y, scale);
		} else {
		// single digit
		oled_draw_digit_scaled(ones, x,                 y, scale);
	}
}

// Show ONLY the current score (label "SC" + big number)
static void oled_show_score(uint8_t score)
{
	uint8_t scaleNum  = 3;
	uint8_t labelY = 6;                  // "SC" near the top
	uint8_t numY   = labelY + 9 + 10;     // 7 px label height + small gap

	ssd1306_clear_buffer();

	// Label "SC" centered at top (scale 1)
	uint8_t len = 2;
	uint8_t scaleLabel = 1;
	uint8_t charCoreW  = 5 * scaleLabel;
	uint8_t gap        = scaleLabel;
	uint8_t charW      = charCoreW + gap;
	uint8_t textW      = len * charW - gap;
	uint8_t startX     = (SSD1306_WIDTH - textW) / 2;
	uint8_t x          = startX;

	oled_draw_char5x7_scaled('S', x, labelY, scaleLabel);
	x += charW;
	oled_draw_char5x7_scaled('C', x, labelY, scaleLabel);

	// Big centered score number
	oled_draw_big_number(score, numY, scaleNum);

	ssd1306_update_display();
}

// Show the global highScore centered on the OLED (label "HI" + big digits) 
static void oled_show_highscore(void)
{
	uint8_t scaleNum  = 3;
	uint8_t labelY = 6;
	uint8_t numY   = labelY + 9 + 10;

	ssd1306_clear_buffer();

	// Label "HI"
	uint8_t len = 2;
	uint8_t scaleLabel = 1;
	uint8_t charCoreW  = 5 * scaleLabel;
	uint8_t gap        = scaleLabel;
	uint8_t charW      = charCoreW + gap;
	uint8_t textW      = len * charW - gap;
	uint8_t startX     = (SSD1306_WIDTH - textW) / 2;
	uint8_t x          = startX;

	oled_draw_char5x7_scaled('H', x, labelY, scaleLabel);
	x += charW;
	oled_draw_char5x7_scaled('I', x, labelY, scaleLabel);

	// Big centered highscore number
	oled_draw_big_number(highScore, numY, scaleNum);

	ssd1306_update_display();
}

// Show current score and high score as:
// Line 1:  SC  <score>
// Line 2:  HI  <high score>
static void oled_show_score_and_highscore(uint8_t score, uint8_t hs)
{
	if (score > 99) score = 99;
	if (hs > 99)    hs    = 99;

	ssd1306_clear_buffer();

	uint8_t scaleLabel = 1;   // small text
	uint8_t scaleNum   = 2;   // bigger numbers

	uint8_t charCoreW  = 5 * scaleLabel;
	uint8_t gapLabel   = scaleLabel;
	uint8_t charW      = charCoreW + gapLabel;

	// Y positions for the two lines
	uint8_t line1Y = 12;                       // first row
	uint8_t digitH = 7 * scaleNum;
	uint8_t line2Y = line1Y + digitH + 10;     // second row

	// X positions: label on left, number to the right
	uint8_t labelX = 10;
	uint8_t numX   = 60;     // move left/right if you want

	// LINE 1: "SC" + score
	uint8_t x = labelX;
	oled_draw_char5x7_scaled('S', x,        line1Y, scaleLabel);
	x += charW;
	oled_draw_char5x7_scaled('C', x,        line1Y, scaleLabel);

	// big number on same horizontal line
	oled_draw_big_number_at(score, numX,    line1Y, scaleNum);

	// LINE 2: "HI" + high score
	x = labelX;
	oled_draw_char5x7_scaled('H', x,        line2Y, scaleLabel);
	x += charW;
	oled_draw_char5x7_scaled('I', x,        line2Y, scaleLabel);

	oled_draw_big_number_at(hs,   numX,     line2Y, scaleNum);

	ssd1306_update_display();
}

//////////////////////////////////
// Tones and Delay
//////////////////////////////////

static void delay_ms(uint16_t ms)
{
	while (ms--) {
		_delay_ms(1);
	}
}

static void playTone(uint16_t freq, uint16_t duration_ms)
{
	if (freq == 0) {
		return;
	}

	uint32_t periods = ((uint32_t)duration_ms * (uint32_t)freq) / 1000UL;
	if (periods == 0) periods = 1;

	uint32_t cycles_per_half = F_CPU / (2UL * (uint32_t)freq);
	uint32_t i;

	while (periods--) {
		PORTB |= (1 << BUZZER_BIT);
		for (i = 0; i < cycles_per_half; i++) {
			_NOP();
		}

		PORTB &= ~(1 << BUZZER_BIT);
		for (i = 0; i < cycles_per_half; i++) {
			_NOP();
		}
	}
}

static void stopTone(void)
{
	PORTB &= ~(1 << BUZZER_BIT);
}

//////////////////////////////////
// ADC for random seed
//////////////////////////////////

static void adc_init(void)
{
	ADMUX  = (1 << REFS0);  // AVcc ref, ADC0
	ADCSRA = (1 << ADEN) |
	(1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0); // prescaler 128
}

static uint16_t adc_read0(void)
{
	ADCSRA |= (1 << ADSC);
	while (ADCSRA & (1 << ADSC)) {
		;
	}
	return ADC;
}

//////////////////////////////////
// USART helpers
//////////////////////////////////

static void usart_init(void)
{
	UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
	UBRR0L = (uint8_t)(UBRR_VALUE & 0xFF);

	UCSR0B = (1 << TXEN0);                  // enable TX
	UCSR0C = (1 << UCSZ01) | (1 << UCSZ00); // 8N1
}

static void usart_send_char(char c)
{
	while (!(UCSR0A & (1 << UDRE0))) {
		;
	}
	UDR0 = c;
}

static void usart_print_string(const char *s)
{
	while (*s) {
		usart_send_char(*s++);
	}
}

static void usart_print_number(uint16_t num)
{
	char buf[6];
	itoa(num, buf, 10);
	usart_print_string(buf);
}

//////////////////////////////////
// Game functions
//////////////////////////////////

static void lightLedAndPlayTone(uint8_t ledIndex)
{
	PORTB |= (1 << led_bits[ledIndex]);
	playTone((uint16_t)gameTones[ledIndex], 50);
	PORTB &= ~(1 << led_bits[ledIndex]);
	stopTone();
}

static void playSequence(void)
{
	for (uint8_t i = 0; i < gameIndex; i++) {
		uint8_t currentLed = gameSequence[i];
		lightLedAndPlayTone(currentLed);
		delay_ms(100);
	}
}

// wait for button with timeout
static bool readButtonsWithTimeout(uint16_t timeout_ms, uint8_t *buttonIndex)
{
	uint32_t start = millis();

	while ((millis() - start) < timeout_ms) {
		for (uint8_t i = 0; i < 4; i++) {
			uint8_t bit = button_bits[i];
			if ((PIND & (1 << bit)) == 0) {   // pressed (LOW)
				delay_ms(20);                 // debounce
				while ((PIND & (1 << bit)) == 0) {
					;
				}
				*buttonIndex = i;
				return true;
			}
		}
		delay_ms(1);
	}
	// timeout
	return false;
}

// Compare user input with expected sequence
static bool checkUserSequence(void)
{
	for (uint8_t i = 0; i < gameIndex; i++) {
		uint8_t expectedButton = gameSequence[i];
		uint8_t actualButton;

		// e.g. 3000 ms timeout per input
		if (!readButtonsWithTimeout(3000, &actualButton)) {
			return false;  // took too long
		}

		lightLedAndPlayTone(actualButton);

		if (expectedButton != actualButton) {
			return false;
		}
	}
	return true;
}

// Game over sequence – takes last successful score
static void gameOver(uint8_t lastScore)
{
	bool newRecord = false;
	if (lastScore > highScore) {
		highScore = lastScore;
		
		eeprom_update_byte(&ee_highScore_eep, highScore);

		newRecord = true;
	}

	usart_print_string("GAME OVER\r\n");

	usart_print_string("YOUR SCORE: ");
	usart_print_number(lastScore);
	usart_print_string("\r\n");

	usart_print_string("HIGH SCORE: ");
	usart_print_number(highScore);
	usart_print_string("\r\n");
	usart_print_string("----------------\r\n");

	// OLED
	oled_show_message("GAme OvEr!!");
	delay_ms(1500);

	oled_show_score_and_highscore(lastScore, highScore);
	delay_ms(4000);

	oled_show_highscore();

	gameIndex = 0;
}

//////////////////////////////////
// Hardware setup
//////////////////////////////////

static void io_init(void)
{
	// Buzzer + LEDs
	DDRB |= (1 << BUZZER_BIT) |
	(1 << PB1) |
	(1 << PB2) |
	(1 << PB3) |
	(1 << PB4);

	// Buttons as inputs with pull-ups
	DDRD &= ~((1 << PD2) | (1 << PD3) | (1 << PD4) | (1 << PD5));
	PORTD |= (1 << PD2) | (1 << PD3) | (1 << PD4) | (1 << PD5);

	// Start button on PD6 with pull-up
	DDRD &= ~(1 << START_BUTTON_BIT);
	PORTD |= (1 << START_BUTTON_BIT);

	// I2C pins (SDA/SCL) pull-up
	PORTC |= (1 << PC4) | (1 << PC5);
}

//////////////////////////////////
// Wait for start button press
//////////////////////////////////

static void wait_for_start_button_press(void)
{
	while (1) {
		if ((PIND & (1 << START_BUTTON_BIT)) == 0) {  // active LOW
			delay_ms(20); // debounce
			while ((PIND & (1 << START_BUTTON_BIT)) == 0) {
				// wait for release
			}
			return;
		}
	}
}

//////////////////////////////////
// main() – game loop
//////////////////////////////////

int main(void)
{
	io_init();
	adc_init();
	usart_init();
	twi_init();          // init I2C for OLED
	ssd1306_init();      // init OLED

	timer1_init();       // init timer for millis / timeout
	sei();               // enable global interrupts

	// LOAD high score from EEPROM at startup
	uint8_t stored = eeprom_read_byte(&ee_highScore_eep);
	if (stored == 0xFF) {
		// first run or erased EEPROM -> treat as 0
		stored = 0;
		eeprom_update_byte(&ee_highScore_eep, stored);
	}
	highScore = stored;

	// show the stored high score on the OLED
	oled_show_highscore();

	usart_print_string("READY - press START button\r\n");

	uint16_t seed = adc_read0();
	srand(seed);

	while (1) {

		// WAIT FOR START BUTTON
		wait_for_start_button_press();

		// Show "START" only when the start button is pressed
		oled_show_message("START");
		delay_ms(2000); 

		// Start with score 0 on OLED (label "SC")
		oled_show_score(0);
		delay_ms(500);

		usart_print_string("START\r\n");

		// Reset game state for a NEW game
		gameIndex = 0;

		// ONE FULL GAME LOOP
		bool playing = true;
		while (playing) {

			// Add a random color to the end of the sequence
			gameSequence[gameIndex] = (uint8_t)(rand() % 4);
			gameIndex++;
			if (gameIndex >= MAX_GAME_LENGTH) {
				gameIndex = MAX_GAME_LENGTH - 1;
			}

			// Play the sequence
			playSequence();

			// Let user try to repeat
			if (!checkUserSequence()) {
				// Last *successful* level is gameIndex - 1 (if > 0)
				uint8_t lastScore = 0;
				if (gameIndex > 0) {
					lastScore = gameIndex - 1;
				}

				gameOver(lastScore); // shows Game Over + scores
				playing = false;     // exit this game, wait for next START
				} else {
				// Correct sequence – show level on serial
				usart_print_string("LEVEL ");
				usart_print_number(gameIndex);
				usart_print_string("\r\n");

				// Update score on OLED (score = current level)
				oled_show_score(gameIndex);

				delay_ms(750);
				if (gameIndex > 0) {
					delay_ms(90);
				}
			}
		}
	}

	return 0;
}


