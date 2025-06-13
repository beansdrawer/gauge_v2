#ifndef __CONSOLE_H__
#define __CONSOLE_H__

#include <Stream.h>       // for HardwareSerial

class Console
{
public:
  Console();
  ~Console();
  
  /**
   * Begin serial port, internally fixed
   */
  void begin(int baud);

  int available();
	
  int read();
 
  int getDebugCommand(char *poutBuff);

public:
  void print(char c);
  void print(const char *str, ...);

  //void println(const char *str);
  void println(const char *str, ...);

private:
  Stream *pStream;
  char buff[256];

private:
  char dbgCmdBuff[64];
  int dbgCmdLen = 0;
};


extern Console console;

#endif 
//__CONSOLE_H__
