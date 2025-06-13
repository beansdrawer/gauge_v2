/**
  Timer utility
 
  using Timer1
*/
#include "TimerOne.h"
#include "LTimer.h"

LTimer ltimer;

LTimer *LTimer::pLTimer = 0;

LTimer::LTimer()
{
  LTimer::pLTimer = this;
}

LTimer::~LTimer()
{
  
}

/*
typedef	struct _sys_timer
{
	uint8	flag;		// bit7:allocated, bit0:running flag
	uint32	value;
} sys_timer;

sys_timer timer_list[MAX_SYS_TIMER];

// internal timer count..
uint32 timerCount;

*/
int LTimer::alloc()
{
	int i;
	for (i=0; i<MAX_SYS_TIMER; i++)
	{
		if ((timer_list[i].flag & 0x80) == 0)
		{
			timer_list[i].flag = 0x80;
			return i;
		}
	}
	return -1;
}

int LTimer::free(int id)
{
	if ( (id < 0) || (id >= MAX_SYS_TIMER) )
	{
		return -1;
	}

  noInterrupts();
	timer_list[id].flag = 0;
    interrupts();
	return 0;
}

int LTimer::set(int timer_id, uint32_t time_value)
{
	if ( (timer_id < 0) || (timer_id >= MAX_SYS_TIMER) )
	{
		return -1;
	}
	
	if (timer_list[timer_id].flag & 0x80)	// allocated
	{
    noInterrupts();
		timer_list[timer_id].value = time_value;
		timer_list[timer_id].flag  = 0x81;
    interrupts();
		return 0;
	}
	else
	{
		return -1;
	}
}

/*
	@return	
		0	: is fired (timeout)
		1	: running (not fired yet)
		-1	: not-running
*/

int LTimer::get(int timer_id)
{
	if ( (timer_id < 0) || (timer_id >= MAX_SYS_TIMER) )
	{
		return -1;
	}

	if (timer_list[timer_id].flag == 0x81)
	{
		if (timer_list[timer_id].value == 0)
			return 0;
		else
			return 1;
	}
	else
	{
		return -1;
	}
}

bool LTimer::isfired(int timer_id)
{
  if (get(timer_id) == 0) return true;
  else return false;
}


int LTimer::clear(int timer_id)
{
	if ( (timer_id < 0) || (timer_id >= MAX_SYS_TIMER) )
	{
		return -1;
	}

	if (timer_list[timer_id].flag & 0x80)	// allocated
	{
    noInterrupts();
		timer_list[timer_id].flag = 0x80;
    interrupts();
		return -1;
	}
	else
	{
		return 0;
	}
}


/**
	initialize timer.
	- timer utility init
	- timer register setting
*/
void LTimer::begin()
{
	for (int i=0; i<MAX_SYS_TIMER; i++)
	{
		timer_list[i].flag  = 0;
		timer_list[i].value = 0;
	}

  Timer1.initialize(1000);

  Timer1.attachInterrupt(ltimer_callback);
}

void LTimer::ltimer_callback()
{
  if (pLTimer == 0) return;

	int i;

	// Need not clear TCNT1, it is automatically cleared on CTC mode.(Clear Timer on Compare match)
	// TCNT1 = 0;
	// 1ms timer.
	pLTimer->timerCount++;

	// timer decreasing.
	for (i=0; i<MAX_SYS_TIMER; i++)
	{
		if (pLTimer->timer_list[i].value > 0)
		{
			pLTimer->timer_list[i].value--;
		}
	}
}
