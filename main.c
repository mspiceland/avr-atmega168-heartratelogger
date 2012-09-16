/* ***********************************************************************
**  Log wireless heart rate signal to SD/MMC card
**  Copyright (C) 2009 Michael Spiceland
*************************************************************************
**
**  This program is free software; you can redistribute it and/or
**  modify it under the terms of the GNU General Public License
**  as published by the Free Software Foundation; either version 2
**  of the License, or (at your option) any later version.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software Foundation, 
**  Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
**
*************************************************************************/
#define F_CPU		8000000
#define BAUD_RATE	4800       // desired baud rate
#define UBRR_DATA	(F_CPU/(BAUD_RATE)-1)/16  // sets baud rate
#define HR_FACTOR	F_CPU/256.0*60.0

#define ALL_INPUT  0x00  // 0000 0000
#define ALL_OUTPUT 0xFF  // 1111 1111

#define LED_ON(x) (PORTC &= ~(1 << x))
#define LED_OFF(x) (PORTC |= (1 << x))

#include <avr/io.h>
#include <avr/interrupt.h>
#include <inttypes.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <util/delay.h>

#include "mmc_if.h"
#include "tff.h"

FATFS fatfs;
FIL logfile;
#define BUFFER_SIZE 32
volatile uint8_t readptr = 0;
volatile uint8_t writeptr = 0;
volatile uint16_t hr_buffer[BUFFER_SIZE];

inline void beep(void)
{
	PORTD |= (1 << 7); // start beep
	_delay_ms(100);
	PORTD &= ~(1 << 7); // clear beep
}

void send_serial(unsigned char byte)
{
	while (!(UCSR0A & (1 << UDRE0)));
	UDR0 = byte;
}

void init_serial(void)
{
	UCSR0B = _BV(RXEN0) | _BV(TXEN0) | (1 << RXCIE0);
	UBRR0L = UBRR_DATA;
}

/***************************************************************************
* double2string
* convert a double to a string and place it in a pre-allocated space
* note: we only need this to be efficient with code space
***************************************************************************/
inline void double2string (double actualTemp, uint8_t* string)
{
	int temp;

	/* prep the string */
	string[4] = '\0';

	temp = (int16_t)(actualTemp); // to include decimal point for display

	string[2] = ((uint8_t)(temp % 10)) | 0x30;
	temp = temp / 10;

	string[1] = ((uint8_t)(temp % 10)) | 0x30;
	temp = temp / 10;

	string[0] = ((uint8_t)(temp % 10)) | 0x30;
	temp = temp / 10;

	if ('0' == string[0])
	{
		string[0] = ' ';
		if ('0' == string[1])
			string[1] = ' ';
	}

	if (('9' == string[0]) && ('0' == string[1]) && ('8' == string[2]))
	{
		string[0] = 'E';
		string[1] = 'R';
		string[2] = 'R';
	}
}

/* Empty on purpose for now */
SIGNAL(SIG_USART_RECV)
{
}

SIGNAL(SIG_OVERFLOW0)
{
	static uint16_t count = 0;

	count++;

	if (count % 31)
	{
		return;
	}

	if (~PINC & (1 << 4)) // if LED_ON(4)
	{
		hr_buffer[writeptr] = 27;
		writeptr = (writeptr + 1) % BUFFER_SIZE;
		beep();
	}
}

SIGNAL(SIG_OVERFLOW1)
{
	LED_ON(4);
}

SIGNAL(SIG_INTERRUPT1)
{
	uint16_t hr;

	/* measure heartbeat */
	hr = TCNT1L;
	hr |= (TCNT1H << 8);
	hr_buffer[writeptr] = hr;
	writeptr = (writeptr + 1) % BUFFER_SIZE;

	/* reset things */
	TCNT1H = 0;
	TCNT1L = 0;
	TCNT0 = 0; // reset status checker
	LED_OFF(4);
}

inline double sample2heartrate (uint16_t sample)
{
	return (HR_FACTOR / (double)sample);
}

int main(void)
{
	int i;
	FRESULT res;
	uint16_t bytes_written;
	uint8_t tmp_string[] = "xxx\n";
	char filename[] = "heartlog001.txt";
	uint8_t fc = 0; // used for file name inc
	double hr;

	DDRC = ALL_OUTPUT;
	DDRD &= ~(1 << 3); // INT1
	DDRD |= (1 << 7); // buzzer

	/* 8-bit timer for error detection */
	TCCR0B |= _BV(CS02) | _BV(CS00); // CLK / 1024
	TCNT0 = 0; // reset the timer
	TIMSK0 |= _BV(TOIE0); // interrupt on overflow

	/* set up external interrupts */
	EICRA |= _BV(ISC10) | _BV(ISC11); // interrupt on rising edge of INT1
	EIMSK |= _BV(INT1); // enable int1 interrupts

	/* 16-bit timer for the PWM out */
	TCCR1B |= _BV(CS12); // clk/256
	TCNT1H = 0;
	TCNT1L = 0;
	TIMSK1 |= _BV(TOIE1); // interrupt on overflow

	/* 8-bit timer for about 8khz timer */

	LED_OFF(1); // power
	LED_ON(2); // sd status
	LED_ON(3); // hr
	LED_ON(4); // hr error

	LED_ON(1);

	// init the uart
	init_serial();

	// setup callbacks for stdio use
	fdevopen(send_serial, NULL);

	// say hello
	printf("\n\r\n\r** heart rate logger **\n\r");

	// init mmc card and report status
	//printf("mmc_init returns %d\n\r", i = mmc_init() );
	i = mmc_init();
	if (i)
	{
		LED_ON(2);
	}
	else
	{
		LED_OFF(2);
	}

	if (f_mount(0, &fatfs))
	{
		LED_ON(2);
		printf ("mount failed\n\r");
	}
	else
	{
		//LED_ON(2);
		printf ("mount successfull\n\r");
	}

	res = FR_NO_FILE;
	while ((FR_OK != res) && (fc < 250)) // we only try 250 times
	{
		snprintf(filename, sizeof(filename), "hlog%03d.txt", fc++);
		printf("res = %d trying %s\n\r", res, filename);
		res = f_open(&logfile, filename, FA_CREATE_NEW | FA_WRITE);
	}
	if (res)
	{
		printf("file open failed\n\r");
		LED_ON(2);
		cli();
	}
	else
	{
		printf("file opened succesfully\n\r");
		sei();
	}

	//sei();
	while (1)
	{
		/* pull value out of buffer */
		while (writeptr == readptr); // block waiting

		hr = sample2heartrate(hr_buffer[readptr]);
		double2string(hr, tmp_string);

		if ((hr < 40) || (hr > 180)) // invalid hr
		{
			beep();
		}

		readptr = (readptr + 1) % BUFFER_SIZE;
		printf("%s\n\r", tmp_string);

		if (PIND & (1 << 3))
		{
			LED_ON(3);
		}
		else
		{
			LED_OFF(3);
		}

		tmp_string[3] = '\n';
		if ((f_write(&logfile, tmp_string, 4, &bytes_written) == FR_OK)
				&& (4 == bytes_written))
		{
			LED_OFF(2);
		}
		else
		{
			LED_ON(2);
			cli(); // disable interrupts
			printf("Oops, only wrote %d bytes\n\r", bytes_written);
		}

		/* sync the data in case we loose power */
		if (f_sync(&logfile) == FR_OK)
		{
			LED_OFF(2);
		}
		else
		{
			LED_ON(2);
			cli(); // disable interrupts
			printf("error syncing to disk.\n\r");
		}
	}

	f_mount(0, NULL);
}
