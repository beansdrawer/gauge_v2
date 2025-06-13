#include "Console.h"
//#include <TimeLib.h>
#include <HardwareSerial.h>
#include "usb_serial.h"           // note. Serial
//#include "SerialUART.h"

#define MAX_PACKET_LENGTH 128
#define CHAR_CR         0x0D
#define CHAR_LF         0x0A

Console console;

Console::Console()
{
  pStream = 0;
}

Console::~Console()
{
  //
}

void Console::begin(int baud)
{
  // Serial7 : for J13(name uart4)
  // Serial : teensy download/debug
  Serial1.begin(baud);
  pStream = &Serial1;

  //println("Console on Serial1");
}

int Console::available()
{
  if (pStream == 0) return 0;
  return pStream->available();
}

int Console::read()
{
  if (pStream == 0) return 0;
  return pStream->read();
}

int Console::getDebugCommand(char *poutBuff)
{
  if (pStream == 0) return 0;

  if (pStream->available())
  {
    char c = pStream->read();

    if (c == '\r')
    {
      pStream->print(c);
      pStream->print('\n');

      int len = dbgCmdLen;

      strcpy(poutBuff, dbgCmdBuff); //, len);
      dbgCmdLen = 0;

      return len;
    }

    else
    {
      if (dbgCmdLen < 63)
      {
        // echo.
        pStream->print(c);

        dbgCmdBuff[dbgCmdLen] = c;
        dbgCmdBuff[++dbgCmdLen] = 0;
      }
      //else no more command..

      return -1;
    }
  }

  return -1;
}

void Console::print(char c)
{
  if (pStream == 0) return;
  pStream->print(c);  
}

void Console::print(const char *format, ...)
{
  if (pStream == 0) return;
  //pStream->print(format);
  
  /*
  time_t _t = now();
  sprintf(buff, "%02d:%02d:%02d ", hour(_t), minute(_t), second(_t));

  int prefixLen = 9;  //strlen(buff);
  
  va_list argList;
  va_start(argList, format);
  vsprintf(buff + prefixLen, format, argList);
  va_end(argList);
  pStream->print(buff);
  */

  
  va_list argList;
  va_start(argList, format);
  vsprintf(buff, format, argList);
  va_end(argList);
  pStream->print(buff);
}

void Console::println(const char *format, ...)
{
  if (pStream == 0) return;
  /*
  pStream->println(format);
  */
  /*
  
  time_t _t = now();
  sprintf(buff, "%02d:%02d:%02d ", hour(_t), minute(_t), second(_t));
  int prefixLen = 9;  //strlen(buff);
  
  va_list argList;
  va_start(argList, format);
  vsprintf(buff + prefixLen, format, argList);
  va_end(argList);
  pStream->println(buff);
  */
  
  va_list argList;
  va_start(argList, format);
  vsprintf(buff, format, argList);
  va_end(argList);
  pStream->println(buff);
}
