/**
	Timer utility for teensy
*/
#ifndef	__LTIMER_H__
#define	__LTIMER_H__

#include "inttypes.h"

#define	timer_isfired(x)	((timer_get(x)==0))

#define MAX_SYS_TIMER 8
typedef  struct _lsys_timer
{
  uint8_t flag;   // bit7:allocated, bit0:running flag
  uint32_t  value;
} lsys_timer;

class LTimer
{
public:
  LTimer();
  ~LTimer();
  
public:
  void begin();

  int alloc();

  int free(int id);

public:
  /*
    time value in milliseconds.
    - about 4000000.000 seconds will be counted.
  */
  int set(int timer_id, uint32_t time_value);
  
  int get(int timer_id);

  bool isfired(int timer_id);
  
  int clear(int timer_id);

private:
  // instance
  static LTimer *pLTimer;
  
  lsys_timer timer_list[MAX_SYS_TIMER];

  // internal timer count..
  uint32_t timerCount;

public:
  static void ltimer_callback();
};

extern LTimer ltimer;

#endif
