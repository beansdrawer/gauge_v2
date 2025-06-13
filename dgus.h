/**
	DGUS interface for arduino, Rasberry Pi Pico
  2024/09/09
*/
#ifndef	__DGUS_GUI_H__
#define	__DGUS_GUI_H__

#include <stdint.h>

/**
	DGUS GUI interface.

	- command
		. set display screen
		. set data

	- key event
	- register (VP) data event
*/
class DGUS 
{
public:
  int init();

  /**
    Set display screen.
  */
  void set_screen(uint16_t pic_id);

  //
  void set_bright(uint8_t bright);

  /**

    ex)
      A5 5A 07 82 01 01 00 02 00 07 		<-- ok. ; note) ���� �ʵ� ����.
                  ----- ----- -----
            addr  d1    d2
  */
  void set_variable(uint16_t addr, int len, uint16_t data[]);


  void set_text(uint16_t addr, int strlen, char *str);

private:
  uint8_t buff[128];

public:
  void process();

};

//-----------------------------------------------------------------------------
/**
	Callback from dwin..
*/
/*
extern void dgus_callback_key_event(uint16_t vp, uint16_t data);
extern void dgus_callback_data_event(uint16_t vp, uint8_t len, uint16_t data);
*/
#endif
