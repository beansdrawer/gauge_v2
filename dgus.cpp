/**
	DGUS interface for arduino, Rasberry Pi Pico
  2024/09/09
*/
#include <SPI.h>
#include "dgus.h"

#include <stdio.h>			// sprintf.

//#include "Serial1.h"
// note) no Serial.h or Serial1.h,  use SPI.h

//-- button positions

#define	HEADER1		0x5A
#define	HEADER2		0xA5

#define	CMD_WRITE_REG	0x80
#define	CMD_READ_REG	0x81

#define	CMD_WRITE_VAR	0x82
#define	CMD_READ_VAR	0x83


//
#define	DGUS_PUTCHAR(c)	Serial2.write(c)

/**
	Display ..

	base pic		- P0.
	icon library 1	- P1.
	icon library 2	- P2.
*/

int DGUS::init()
{
	Serial2.begin(115200);

	return 0;
}

//-----------------------------------------------------------------------------
// basic functions
//
/**
	DGUS GUI interface.

	- command
		. set display screen
		. set data

	- key event
	- register (VP) data event
*/


void DGUS::set_screen(uint16_t pic_id)
{
	uint8_t	cmd[32];
	int len = 0;
	int i;

	cmd[len++] = HEADER1;
	cmd[len++] = HEADER2;
	cmd[len++] = 7;						// baudrate 115200
	cmd[len++] = 0x82;				// Write VP..
	cmd[len++] = 0x00;
	cmd[len++] = 0x84;
	cmd[len++] = 0x5a;
	cmd[len++] = 0x01;
	cmd[len++] = pic_id >> 8;
	cmd[len++] = pic_id;

	for (i=0; i<len; i++)
	{
		DGUS_PUTCHAR(cmd[i]);
	}	//
}

/**
	..
	0x00 ~ 0x40.
*/
void DGUS::set_bright(uint8_t bright)
{
	uint8_t	cmd[32];
	int len = 0;
	int i;

	if (bright > 0x40) bright = 0x40;
	
	cmd[len++] = HEADER1;
	cmd[len++] = HEADER2;
	cmd[len++] = 3;
	cmd[len++] = CMD_WRITE_REG;
	cmd[len++] = 0x01;
	cmd[len++] = bright;

	for (i=0; i<len; i++)
	{
		DGUS_PUTCHAR(cmd[i]);
	}	//
}


/**

	ex)
		A5 5A 07 82 01 01 00 02 00 07 		<-- ok. ; note) ���� �ʵ� ����.
		            ----- ----- -----
					addr  d1    d2
*/
void DGUS::set_variable(uint16_t addr, int ndata, uint16_t data[])
{
	uint8_t	cmd[128];
	int len = 0;
	int i;

	cmd[len++] = HEADER1;
	cmd[len++] = HEADER2;
	cmd[len++] = 2 + ndata*2 + 1;
	cmd[len++] = CMD_WRITE_VAR;
	cmd[len++] = addr >> 8;
	cmd[len++] = addr;

	for (i=0; i<ndata; i++)
	{
		cmd[len++] = data[i] >> 8;
		cmd[len++] = data[i];
	}

	for (i=0; i<len; i++)
	{
		DGUS_PUTCHAR(cmd[i]);
	}
}


/**
	Set text to var.
*/
void DGUS::set_text(uint16_t addr, int strlen, char *str)
{
	uint8_t	cmd[128];
	int len = 0;
	int i;

	cmd[len++] = HEADER1;
	cmd[len++] = HEADER2;
	cmd[len++] = 2 + strlen + 1;
	cmd[len++] = CMD_WRITE_VAR;
	cmd[len++] = addr >> 8;
	cmd[len++] = addr;

	for (i=0; i<strlen; i++)
	{
		cmd[len++] = str[i];
	}

	for (i=0; i<len; i++)
	{
		DGUS_PUTCHAR(cmd[i]);
	}
}



//-----------------------------------------------------------------------------
// positions in main
//


/**
	parse touch event.
	- see callback. event_key()..
*/
void DGUS::process()
{
	// parsing state.
	static uint8_t state = 0;
	static uint8_t length = 0;
	static uint8_t cmd = 0;			// internal state.
	static int cnt = 0;			// internal state.
	unsigned char c;
	
	//while (uart8_getchar(&c) == 1)
  while (Serial2.available())
	{
    c = Serial2.read();
		//printf("dr:state=%d, %02x\n", state, c);
		switch (state)
		{
		case 0:		// not started.
			{
				if (c == HEADER1)
				{
					state = 1;
				}
				// else state = 0
			}
			break;

		case 1:		// not started.
			{
				if (c == HEADER2)
				{
					state = 2;
				}
				else
				{
					state = 0;
				}
			}
			break;

		case 2:	// length field
			{
				length = c;
				state = 10;
			}
			break;

		case 10:	// cmd
			{
				cmd = c;

				if (cmd == 0x81)
				{
					state = 20;
					// ?.
				}
				else if (cmd == 0x83)	// VP data.
				{
					//
					state = 30;
					cnt = 0;
				}
				else if (cmd == 0x82)	// VP response
				{
					state = 31;
					cnt = 0;
				}
			}
			break;


		case 30:	// {addr(2), len(1), data(2 * len)}
			{
				if (cnt < (length - 1))
				{
					buff[cnt++] = c;
				}

				if (cnt == 5)
				{
					// length == 6. always.
					uint16_t	vp = (buff[0] << 8) | buff[1];
					uint8_t	len = buff[2];
					uint16_t	data = (buff[3] << 8) | buff[4];

					// command done --> callback.
					//dgus_callback_data_event(vp, len, data);

					state = 0;
				}
			}
			break;

		case 31:	// {addr(2), len(1), data(2 * len)}
			{
				if (cnt < (length - 1))
				{
					buff[cnt++] = c;
				}
				if (cnt == (length-1))
				{
					state = 0;
				}
			}
			break;
		}
		// end switch.
		//printf("-> dr:state=%d, %02x\n", state, c);
	}
	// end while.
}
