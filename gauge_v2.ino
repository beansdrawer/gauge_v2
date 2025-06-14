/*
  Gauge 2.0
*/
#include <EEPROM.h>
#include "LTimer.h"
#include "Console.h"
#include "dgus.h"

#include <NativeEthernet.h>
#include <NativeEthernetUdp.h>

#include <cstring>

#define LED_TIMER_INTERVAL 500
#define INTRO_TIMER_INTERVAL 5000
#define CONFIG_MAIN_TIMER_INTERVAL 30000

#define PAGE_INTRO 0
#define PAGE_WARN 1
#define PAGE_CONFIG_MAIN 2
#define PAGE_CONFIG_BASIC 3
#define PAGE_CONFIG_NETWORK_DHCP 4
#define PAGE_CONFIG_NETWORK_MANUAL 5
#define PAGE_RPM 7
#define PAGE_ROT 8
#define PAGE_RA 9

#define CONFIG_MARK   "FHTG"
#define RANGE_RA    56        // RUDDER_ANGLE : -RANGE_RA ~ RANGE_RA

typedef struct _dev_config
{
  int pid;                // 0
  int did;                // 1..n for device (communication) id
  int type;               // 1..3
  int net_mode;           // 0 : DHCP, 1 : MANUAL
  uint8_t net_ip[4];      // ip
  uint8_t net_gw[4];      // gw
  uint8_t net_sm[4];      // subnet mask
  int port;               // default 8888
  uint8_t mac[6];         // mac address

  char mark[4];           // default "HNSX"
} Config;

enum {
  ST_NONE,                // waiting
  ST_CONFIG,
  ST_STARTING,            // try connect net..
  ST_NET_READY,           // ethernet open ok
  ST_READY,
};

Config config;
Config configBackUp;
// Enter a MAC address for your controller below.
byte mac[] = {
  //0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED
  0xDE, 0xAD, 0xBE, 0xEF, 0x1E, 0xED
};

const int csPin = 20; // Chip Select (CS) pin for W5100S on W5100S-EVB-Pico
//const int localPort = 9999; // Local port to listen on

char packetBuffer[UDP_TX_PACKET_MAX_SIZE*2]; // Buffer to hold incoming packets

EthernetUDP Udp;
static int udpStatus = 0;

static int state = 0;     // overall state
    // 0: not yet
    // 1: ethernet ok
    // 2: udp ok

static int debug = 0;

static int target_rpm = 0;
static int target_rot = 0;
static int target_ra = 0;

static int current_rpm = 0;
static int current_rot = 0;
static int current_ra = 0;

static int tid_led;
static int tid_op;
static int tid_update;
static int tid_config;
static int tid_configMain;

static bool isInConfigPage = false;
static int hiddenKeyTouchCnt = 0;

DGUS dgus;

static int cmdLen;
static char cmdBuff[64];

// Return Key Code Command
static int retCmdLen = 0;
static char retCmdBuff[64];

static void process_command(char *cmd, int len);

static void processPacket(char *packet);
static uint16_t map_angle(int32_t angle);
static uint16_t map_angle_ra(int32_t angle);

static void set_values(int rpm, int rot, int ra);

static void update_values();

static int makeDefaultConfig(Config *pc);
static bool isValidConfig(Config *pc);
static int loadConfig(Config *pc);
static int saveConfig(Config *pc);

// to parse text input 
static int inputDataParseInt(char * buff);

// 
static void setBasicConfigValues();
static void setNetworkConfigValues();

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);

  ltimer.begin();
  tid_led = ltimer.alloc();
  tid_op = ltimer.alloc();
  tid_update = ltimer.alloc();
  tid_config = ltimer.alloc();
  tid_configMain = ltimer.alloc();
  
  console.begin(115200);
  Serial1.println("GAUGE 2.0 ForHumanTech developing Started...");

  // put your setup code here, to run once:
  delay(100);
  Serial.begin(115200);

  loadConfig(&config);
  Serial.println(config.mark);
  configBackUp = config;
  if (!isValidConfig(&config))
  {
    makeDefaultConfig(&config);
    configBackUp = config;
    saveConfig(&config);
    console.println("Configuration made default and saved");
  }
  else
  {
    console.println("Configuration ok");
  }

  dgus.init();

  delay(100);

  ltimer.set(tid_led, LED_TIMER_INTERVAL); 
  ltimer.set(tid_config, INTRO_TIMER_INTERVAL); 

  dgus.set_screen(PAGE_INTRO);
}


void loop() 
{
  if(ltimer.isfired(tid_config))
  { 
    if(!isInConfigPage)
    {
      isInConfigPage = true;
      // dgus.set_screen(PAGE_RPM);
      switch (config.type) 
      {
        case 1 :
          dgus.set_screen(PAGE_RPM);
          break;
        case 2 :
          dgus.set_screen(PAGE_ROT);
          break;
        case 3 :
          dgus.set_screen(PAGE_RA);
          break;
      }
    }
  }

  if(ltimer.isfired(tid_configMain))
  {  
    if(!isInConfigPage)
    {
      isInConfigPage = true;
      // dgus.set_screen(PAGE_RPM);
      switch (config.type) 
      {
        case 1 :
          dgus.set_screen(PAGE_RPM);
          break;
        case 2 :
          dgus.set_screen(PAGE_ROT);
          break;
        case 3 :
          dgus.set_screen(PAGE_RA);
          break;
      }
    }
  }
  // put your main code here, to run repeatedly:
  if (ltimer.isfired(tid_led))
  {
    static char b = 0;
    ltimer.set(tid_led, LED_TIMER_INTERVAL);
    
    if (b == 0) digitalWrite(LED_BUILTIN, HIGH);
    else digitalWrite(LED_BUILTIN, LOW);

    b ^= 1;
  } 


  while (Serial2.available())
  {
    char byteIn = Serial2.read();

    //Serial.println(byteIn, HEX);

    if(retCmdLen == 0 && byteIn != 0x5A) continue;
    if(retCmdLen == 1 && byteIn != 0xA5) 
    {
      retCmdLen = 0;
      continue;
    }

    retCmdBuff[retCmdLen++] = byteIn;

    // when recieved by data length
    if(retCmdLen == retCmdBuff[2] + 3)
    {
      // Serial.print("retCmdLen : ");
      // Serial.println(retCmdLen);
      console.println(retCmdBuff);
      // for(int j = 0; j < retCmdLen; j++){
      //   Serial.print(retCmdBuff[j], HEX);
      //   Serial.print(' ');
      // }
      // Serial.println("\n");
      if(retCmdBuff[3] == 0x83) 
      {
        uint16_t vp = retCmdBuff[4] << 8 | retCmdBuff[5];
        uint16_t keyVal = retCmdBuff[7] << 8 | retCmdBuff[8];
        Serial.print("VP : ");
        Serial.println(vp, HEX);
        Serial.print("Key Value : ");
        Serial.println(keyVal, HEX);
        // Serial.print(' ');

        // Return Key Handling Start
        if ((vp == 0x0010 || vp == 0x0020) && keyVal == 1) 
        {
          hiddenKeyTouchCnt++;
          if(hiddenKeyTouchCnt == 2)
          {
            ltimer.clear(tid_config);
            
            ltimer.set(tid_configMain, CONFIG_MAIN_TIMER_INTERVAL); 
            dgus.set_screen(PAGE_CONFIG_MAIN);
          }
        }
        else if (vp == 0x0030) // Basic or Network
        {
          ltimer.clear(tid_configMain);
          if(keyVal == 0) 
          { 
            dgus.set_screen(PAGE_CONFIG_BASIC); 
            setBasicConfigValues();
          }
          else if(keyVal == 1) 
          {
            if (config.net_mode == 0) dgus.set_screen(PAGE_CONFIG_NETWORK_DHCP);
            else dgus.set_screen(PAGE_CONFIG_NETWORK_MANUAL);

            setNetworkConfigValues();
          }
        }
        else if (vp == 0x0040)
        {
          if (keyVal == 0)
          {
            tid_config = ltimer.alloc();
            ltimer.set(tid_config, INTRO_TIMER_INTERVAL);
            hiddenKeyTouchCnt = 0;
            dgus.set_screen(PAGE_INTRO);
          }
          else if (keyVal == 1)
          {
            // Save & Exit
            // saveConfig(&config);
            // dgus.set_screen(PAGE_RPM);
            switch (config.type) 
            {
              case 1 :
                dgus.set_screen(PAGE_RPM);
                break;
              case 2 :
                dgus.set_screen(PAGE_ROT);
                break;
              case 3 :
                dgus.set_screen(PAGE_RA);
                break;
            }
          }
        }
        else if (vp == 0x0050 || vp == 0x0060)
        {
          if (keyVal == 0)
          {
            // Cancel & Back
            config = configBackUp;
            tid_configMain = ltimer.alloc();
            ltimer.set(tid_configMain, CONFIG_MAIN_TIMER_INTERVAL); 
            dgus.set_screen(PAGE_CONFIG_MAIN);
          }
          else if (keyVal == 1)
          {
            // Save & Back
            configBackUp = config;
            saveConfig(&config);
            tid_configMain = ltimer.alloc();
            ltimer.set(tid_configMain, CONFIG_MAIN_TIMER_INTERVAL); 
            dgus.set_screen(PAGE_CONFIG_MAIN);
          }
        }
        else if (vp == 0x2000) // Product ID
        {
          uint16_t data[] = {keyVal};
          // data[0] = keyVal;
          config.pid = keyVal;
          dgus.set_variable(0x1000, 1, data);
        }
        else if (vp == 0x2010) // Display Type
        {
          config.type = keyVal;
          switch (config.type) 
          {
            case 1 : 
              dgus.set_text(0x1010, 5, "RPM  ");
              break;
            case 2 : 
              dgus.set_text(0x1010, 5, "ROT  ");
              break;
            case 3 : 
              dgus.set_text(0x1010, 5, "RA   ");
              break;
          }
        }
        else if (vp == 0x2020) // Group ID
        {
          uint16_t data[] = {keyVal};
          config.did = keyVal;
          dgus.set_variable(0x1020, 1, data);
        }
        else if (vp == 0x2030) // UDP PORT (TEXT)
        { 
          config.port = inputDataParseInt(retCmdBuff);

          char buffer[10] = {0,0,0,0,0,0,0,0,0,0};
          sprintf(buffer, "%d", config.port);

          if (strlen(buffer) < 5) 
          {
            for (int i = strlen(buffer); i < 5; i++)
            {
              buffer[i] = ' ';
            }
            buffer[5] = '\0';
          }

          dgus.set_text(0x1030, 5, buffer);
        }
        else if (vp == 0x2040) // Update ID
        {
          switch (keyVal) 
          {
            case 0 : 
              dgus.set_text(0x1040, 6, "AUTO  ");
              break;
            case 1 : 
              dgus.set_text(0x1040, 6, "MANUAL");
              break;
          }
        } // end of config basic
        else if (vp == 0x2050) // Network Mode
        {
          config.net_mode = keyVal;
          
          switch (config.net_mode) 
          {
            case 0 : 
              dgus.set_text(0x1050, 6, "DHCP  ");
              dgus.set_screen(PAGE_CONFIG_NETWORK_DHCP);
              break;
            case 1 : 
              dgus.set_text(0x1050, 6, "MANUAL");
              dgus.set_screen(PAGE_CONFIG_NETWORK_MANUAL);
              break;
          }
        }
        else if (vp >= 0x2060 && vp < 0x2070)
        {
          config.net_ip[(vp - 0x2060) / 4] = inputDataParseInt(retCmdBuff);
        }
        else if (vp >= 0x2080 && vp < 0x2090)
        {
          config.net_gw[(vp - 0x2080) / 4] = inputDataParseInt(retCmdBuff);
        }
        else if (vp >= 0x2100 && vp < 0x2110)
        {
          config.net_sm[(vp - 0x2100) / 4] = inputDataParseInt(retCmdBuff);
        }
      }
      retCmdLen = 0;
    }

  }

  if ((cmdLen=console.getDebugCommand(cmdBuff)) > 0)
  {
    process_command(cmdBuff, cmdLen);
  }

  if (state == ST_READY)
  {
    // Check if there are any incoming UDP packets
    int packetSize = Udp.parsePacket();

    if (packetSize == 28)
    {
      // Read the packet into the buffer
      Udp.read(packetBuffer, 28); //UDP_TX_PACKET_MAX_SIZE);

      processPacket28(packetBuffer);

      // Clear the packet buffer
      memset(packetBuffer, 0, 28); //UDP_TX_PACKET_MAX_SIZE);

    }
    else if (packetSize == 24)
    {
      if (debug > 1)
      {
        Serial1.print("Received packet of size ");
        Serial1.println(packetSize);
        Serial1.print("From ");
        IPAddress remoteIP = Udp.remoteIP();
        Serial1.print(remoteIP);
        Serial1.print(", port ");
        Serial1.println(Udp.remotePort());
      }

      // Read the packet into the buffer
      Udp.read(packetBuffer, 24); //UDP_TX_PACKET_MAX_SIZE);
      // format : ver(4), cmd(4), size(4), rpm(4), rot(4), ra(4)

      // Print the received message
      /*
      Serial1.print("Contents: ");

      for (int i=0; i<24; i++)
      {
        Serial1.print(packetBuffer[i], HEX); Serial1.print(",");
      }
      Serial1.println();      
      */
      processPacket(packetBuffer);

      // Clear the packet buffer
      memset(packetBuffer, 0, 24);  //UDP_TX_PACKET_MAX_SIZE);
    }
    else
    {
      //discarded..
    }
  }

  switch (state)
  {
    case ST_NONE:
      if (cmdLen >= 0)
      {
        Serial1.println("Go config by user\n");
        state = ST_CONFIG;
      }
      if (ltimer.isfired(tid_op))
      {
        //ltimer.set(tid_op, 100);
        Serial1.println("Go start network by timeout\n");
        state = ST_STARTING;
      }
      break;

    case ST_CONFIG:
      // do nothing..
      if (ltimer.isfired(tid_op))
      {
        Serial1.println("CONFIG: Go start network by timeout\n");
        state = ST_STARTING;
      }

      if (cmdLen >= 0)
      {
        // when user commands exist, extends time
        ltimer.set(tid_op, 20000);
      }
      break;
    case ST_STARTING:
      if (config.net_mode == 0)
      {
        // Start the Ethernet connection using DHCP:
        Serial1.println("Starting Ethernet connection using DHCP...");
        if (Ethernet.begin(config.mac, 15000) == 0)   // 15 seconds timeout
        {
          // If DHCP configuration fails, print an error message:
          Serial1.println("Failed to configure Ethernet using DHCP");

          // Try to configure Ethernet with the static IP address:
          //Ethernet.begin(mac, ip);
          state = ST_CONFIG;
          Serial1.println(" - can retry by 'run' command");
          Serial1.println(" - or will retry in 20 seconds");

          ltimer.set(tid_op, 20000);
        }
        else
        {
          Serial1.println("- DHCP ready");
          state = ST_NET_READY;  // ok
        }
      }
      else
      {
        Serial1.println("Starting Ethernet connection using static IP...");

        // Try to configure Ethernet with the static IP address:
        IPAddress ip(config.net_ip[0], config.net_ip[1], config.net_ip[2], config.net_ip[3]);

        Ethernet.begin(config.mac, ip);
        state = ST_NET_READY;  // ok
      }
      break;

    case ST_NET_READY:
      if (Udp.begin(config.port) == 1)
      {
        console.println("UDP open port = %d", config.port);

        // set page by type : 1,2,3 == rpm, rot, ra
        Serial1.print("Set page to :");
        Serial1.println(config.type);

        dgus.set_screen(config.type);

        state = ST_READY;
      }
      break;

    case ST_READY:
      // update current values to target values step by step
      if (ltimer.isfired(tid_update))
      {
        ltimer.set(tid_update, 5);

        Serial1.println("update..");
        // check need update and then update
        update_values();
      }
      break;
  }
  // end switch(state)
  
}
// end main-loop()

#define DIFF_STEP 10
#define DIFF_STEP_RA 2

/*
  if current values are not same with target values, then make it equal step by step
  current_(rpm,rot,ra) to target_(rpm,rot,ra)
*/
static void update_values()
{
  int needUpdate = 0;

  if (current_rpm != target_rpm)
  {
    int diff = target_rpm - current_rpm;
    if (diff > DIFF_STEP) diff = DIFF_STEP;
    else if (diff < -DIFF_STEP) diff = -DIFF_STEP;

    current_rpm = current_rpm + diff;

    needUpdate = 1;
  }
  
  if (current_rot != target_rot)
  {
    int diff = target_rot - current_rot;
    if (diff > DIFF_STEP) diff = DIFF_STEP;
    else if (diff < -DIFF_STEP) diff = -DIFF_STEP;

    current_rot = current_rot + diff;

    needUpdate = 1;
  }
  
  if (current_ra != target_ra)
  {
    int diff = target_ra - current_ra;
    if (diff > DIFF_STEP_RA) diff = DIFF_STEP_RA;
    else if (diff < -DIFF_STEP_RA) diff = -DIFF_STEP_RA;

    current_ra = current_ra + diff;

    needUpdate = 1;
  }

  if (needUpdate == 1)
  {
    set_values(current_rpm, current_rot, current_ra);

    char buff[128];

    sprintf(buff, "Set rpm=%d, rot=%d, ra=%d", current_rpm, current_rot, current_ra);
    Serial1.println(buff);
  }
  else
  {
    ltimer.clear(tid_update);
  }
}

static uint32_t char4_to_uint32(char *p)
{
  uint32_t value = 0;
  value = p[0];  value <<= 8;
  value |= p[1];  value <<= 8;
  value |= p[2];  value <<= 8;
  value |= p[3];

  return value;
}

static void processPacket28(char *packet)
{
  uint32_t pkt_ver = char4_to_uint32(packet);
  uint32_t pkt_cmd = char4_to_uint32(packet+4);
  uint32_t pkt_size = char4_to_uint32(packet+8);

  uint32_t pkt_did = char4_to_uint32(packet+12);
  int32_t pkt_rpm = (int32_t)char4_to_uint32(packet+16);
  int32_t pkt_rot = (int32_t)char4_to_uint32(packet+20);
  int32_t pkt_ra  = (int32_t)char4_to_uint32(packet+24);


  char buff[128];

  sprintf(buff, "V:%08lX,C:%08lX,S:%08lX, did=%ld, rpm=%ld, rot=%ld, ra=%ld",
          pkt_ver, pkt_cmd, pkt_size, pkt_did, pkt_rpm, pkt_rot, pkt_ra);
  Serial1.println(buff);

  if (pkt_cmd != 100)  // msg type = 100
  {
    return;
  }
  if (pkt_did != config.did)
  {
    return;
  }

  // save target values
  target_rpm = pkt_rpm;
  target_rot = pkt_rot;
  target_ra  = pkt_ra;

  if (target_rpm < -320) target_rpm = -320;
  else if (target_rpm > 320) target_rpm = 320;

  if (target_rot < -320) target_rot = -320;
  else if (target_rot > 320) target_rot = 320;

  if (target_ra < -RANGE_RA) target_ra = -RANGE_RA;
  else if (target_ra > RANGE_RA) target_ra = RANGE_RA;

  // update by timer..
  ltimer.set(tid_update, 100);
}

static void processPacket(char *packet)
{
  uint32_t pkt_ver = char4_to_uint32(packet);
  uint32_t pkt_cmd = char4_to_uint32(packet+4);
  uint32_t pkt_size= char4_to_uint32(packet+8);
  int32_t pkt_rpm = (int32_t)char4_to_uint32(packet+12);
  int32_t pkt_rot = (int32_t)char4_to_uint32(packet+16);
  int32_t pkt_ra  = (int32_t)char4_to_uint32(packet+20);

  
  char buff[128];

  sprintf(buff, "V:%08lX,C:%08lX,S:%08lX, rpm=%ld, rot=%ld, ra=%ld",
          pkt_ver, pkt_cmd, pkt_size, pkt_rpm, pkt_rot, pkt_ra);
  Serial1.println(buff);

  if (pkt_cmd != 0x14)  // msg type = 20
  {
    // ignore other msgs
    return;
  }

#if 0
  // addr : 0x02000
  // 3 x uint16_t
  // 
  uint16_t data[3];
  // map -320 ~ 320 to 360+40 ~ 720, 0 ~ 360-40
  data[0] = map_angle(pkt_rpm);
  data[1] = map_angle(pkt_rot);
  data[2] = map_angle(pkt_ra);

  dgus.set_variable(0x0200, 3, data);
#else
  // save target values
  target_rpm = pkt_rpm;
  target_rot = pkt_rot;
  target_ra  = pkt_ra;

  if (target_rpm < -320) target_rpm = -320;
  else if (target_rpm > 320) target_rpm = 320;

  if (target_rot < -320) target_rot = -320;
  else if (target_rot > 320) target_rot = 320;

  if (target_ra < -RANGE_RA) target_ra = -RANGE_RA;
  else if (target_ra > RANGE_RA) target_ra = RANGE_RA;

  // update by timer..
  ltimer.set(tid_update, 100);

#endif
}

static void set_values(int rpm, int rot, int ra)
{
  uint16_t data[3];
  // map -320 ~ 320 to 360+40 ~ 720, 0 ~ 360-40
  data[0] = map_angle(rpm);
  data[1] = map_angle(rot);
  data[2] = map_angle_ra(ra);

  dgus.set_variable(0x0200, 3, data);
}

static uint16_t map_angle(int32_t angle)
{
  uint16_t value = 0;
  int32_t absangle = (angle > 0) ? angle : -angle;
  if (absangle > 320) absangle = 320;
  absangle *= 0.9;

  if (angle > 0) value = absangle;
  else
    value = 720 - absangle;

  /* -- fail on minus..
  if (angle < -320)
  {
    value = 720 - 320;
  }
  else if (angle < 0)
  {
    value = 720 + angle;    // 720 - (0 ~ 320)
  }
  else if (angle > 320)
  {
    value = 320;
  }
  else // 0..320
  {
    value = angle;
  }

  value *= 0.9; // 180/200
  */
  return value;
}
/*
  -55 ~ 55

  // map -320 ~ 320 to 360+40 ~ 720, 0 ~ 360-40

      map : 0 ~ 72   to 0 ~ 360
            0 ~ 56 to 0 ~ 320
            -56 ~ 0 : 720 - (x)

*/
static uint16_t map_angle_ra(int32_t angle)
{
  uint16_t value = 0;
  int32_t absangle = (angle > 0) ? angle : -angle;
  if (absangle > 56) absangle = 56;
  absangle = (int32_t)(absangle * 5.0);

  if (angle > 0) value = absangle;
  else
    value = 720 - absangle;

  return value;
}

static void process_command(char *cmd, int len)
{
  if (strcmp(cmdBuff, "h") == 0)
  {
    // sample.
    //console.println(MY_VERSION);    // "Surface Buoy v1.0.0"
    Serial1.println(" cfg?          : show configuration");
    Serial1.println(" pid=<d>       : set product id(rel. mac");
    Serial1.println(" port=<n>      : set UDP port");
    Serial1.println(" did=<d>       : set device id(rel. communication id");
    Serial1.println(" type=<n>      : set type of gauge 1:RPM, 2:ROT, 3:RA");
    Serial1.println(" NET=<n>       : set net mode 0:DHCP, 1:Static");
    Serial1.println(" IP=<n.n.n.n>  : set IP v4 address");
    Serial1.println(" GW=<n.n.n.n>  : set Gateway address");
    Serial1.println(" SM=<n.n.n.n>  : set Subnet Mask");
    Serial1.println(" save          : save changes");
    Serial1.println(" run           : start network at config mode");
    Serial1.println(" reboot        : reboot");
  }
  else if (strncmp("run", cmd, 3) == 0)
  {
    if (state == ST_CONFIG)
    {
      console.println("Starting network by command");
      state = ST_STARTING;
    } 
  }
  else if (strncmp("port=", cmd, 5) == 0)
  {
    int port = atoi(cmd+5);
    console.println("set UDP port : %d", port);
    if ((port < 1000) || (port > 32760))
    {
      console.println("Error: Port range 2000 ~ 32760");
    }
    else
    {      
      config.port = port;
      console.println("OK");
    }
  }
  else if (strncmp("did=", cmd, 4) == 0)
  {
    int did = atoi(cmd+4);
    console.println("set did : %d", did);
    if ((did < 1) || (did > 65535))
    {
      console.println("Error: did 1..65535");
    }
    else
    {      
      config.did = did;
      console.println("OK");
    }
  }
  else if (strncmp("type=", cmd, 5) == 0)
  {
    int type = atoi(cmd+5);
    console.println("set type : %d", type);
    if ((type < 1) || (type > 3))
    {
      console.println("Error: type 1,2,3");
    }
    else
    {      
      config.type = type;
      console.println("OK");
    }
  }
  else if (strncmp("pid=", cmd, 4) == 0)
  {
    Config *pc = &config;

    int pid = atoi(cmd+4);
    console.println("set id : %d", pid);
    
    //set mac[3..5] to mac[3..5] + pid
    uint32_t tmac = 0xEF1EED + pid;

    pc->mac[3] = (uint8_t)((tmac & 0x00FF0000) >> 16);
    pc->mac[4] = (uint8_t)((tmac & 0x0000FF00) >> 8);
    pc->mac[5] = (uint8_t)(tmac & 0x000000FF);

    console.println("set mac to mac[3..5] = 0x%02X,%02X,%02X", pc->mac[3], pc->mac[4], pc->mac[5]);

    config.pid = pid;
  }
  else if (strncmp("NET=", cmd, 4) == 0)
  {
    int nm = atoi(cmd+4);
    if ((nm < 0) || (nm > 1))
    {
      console.println("Error: network mode 0:DHCP, 1:Static");
    }

    console.println("set network mode : %d", nm);

    config.net_mode = nm;
  }
  else if (strncmp("IP=", cmd, 3) == 0)
  {
    IPAddress ip;

    if (ip.fromString(cmd+3))
    {
      console.print("Set ip to :");
      //console.println(ip.toString());
      Serial1.println(ip);

      config.net_ip[0] = ip[0];
      config.net_ip[1] = ip[1];
      config.net_ip[2] = ip[2];
      config.net_ip[3] = ip[3];
    }
    else
    {
      console.println("invalid format : %s", cmd+3);
    }
  }  
  else if (strncmp("GW=", cmd, 3) == 0)
  {
    IPAddress ip;

    if (ip.fromString(cmd+3))
    {
      console.print("Set gateway to :");
      //console.println(ip.toString());
      Serial1.println(ip);

      config.net_gw[0] = ip[0];
      config.net_gw[1] = ip[1];
      config.net_gw[2] = ip[2];
      config.net_gw[3] = ip[3];
    }
    else
    {
      console.println("invalid format : %s", cmd+3);
    }
  }  
  else if (strncmp("SM=", cmd, 3) == 0)
  {
    IPAddress ip;

    if (ip.fromString(cmd+3))
    {
      console.print("Set subnet mask to :");
      //console.println(ip.toString());
      Serial1.println(ip);

      config.net_sm[0] = ip[0];
      config.net_sm[1] = ip[1];
      config.net_sm[2] = ip[2];
      config.net_sm[3] = ip[3];
    }
    else
    {
      console.println("invalid format : %s", cmd+3);
    }
  }
  else if (strcmp("ei0", cmd) == 0)
  {
    Ethernet.init(csPin);
    if (Ethernet.begin(mac) == 0) {
      Serial1.println("Failed to obtain IP address using DHCP");
    } 
  }
  else if (strcmp("ei1", cmd) == 0)
  {
    Ethernet.init(csPin);
    IPAddress ip(192,168,10,80);
    // Try to configure Ethernet with the static IP address:
    Ethernet.begin(mac, ip);
  }
  else if (strcmp("u1", cmd) == 0)
  {
    udpStatus = 1;
    Serial1.println("udp:on");
  }
  else if (strcmp("u0", cmd) == 0)
  {
    udpStatus = 0;
    Serial1.println("udp:off");
  }
  else if (strcmp("p0", cmd) == 0)
  {
    Serial1.println("Set page 0");
    dgus.set_screen(0);
  }
  else if (strcmp("p1", cmd) == 0)
  {
    Serial1.println("Set page 1");
    dgus.set_screen(1);
  }
  else if (strcmp("p2", cmd) == 0)
  {
    Serial1.println("Set page 2");
    dgus.set_screen(2);
  }
  else if (strcmp("p3", cmd) == 0)
  {
    Serial1.println("Set page 3");
    dgus.set_screen(3);
  }
  else if (strcmp("cfg?", cmd) == 0)
  {
    console.println("GAUGE 1.0 Configurations");
    console.println("PID   : %d", config.pid);
    console.println("DID(communication)   : %d", config.did);
    console.println("TYPE  : %d", config.type);
    console.println("Port  : %d", config.port);
    console.println("NET(0:DHCP,1:MANUAL) : %d", config.net_mode);

    console.println("MAC   : 0x%02X %02X %02X %02X %02X %02X", 
        config.mac[0], config.mac[1], config.mac[2],
        config.mac[3], config.mac[4], config.mac[5]);

    if (config.net_mode == 1) // only manual
    {
      Serial1.println("Manual IP address +++ (Current mode)");
      Serial1.print("IP : ");
      IPAddress ip(config.net_ip[0], config.net_ip[1], config.net_ip[2], config.net_ip[3]);
      Serial1.println(ip);

      Serial1.print("GW : ");
      IPAddress gw(config.net_gw[0], config.net_gw[1], config.net_gw[2], config.net_gw[3]);
      Serial1.println(gw);

      Serial1.print("SM : ");
      IPAddress sm(config.net_sm[0], config.net_sm[1], config.net_sm[2], config.net_sm[3]);
      Serial1.println(sm);
      //Serial1.println("Manual IP address +++");
    }

    // Print the network configuration obtained during Ethernet initialization:

    if (config.net_mode == 1) // only manual
    {
      Serial1.println("Dynamic IP address +++ (Not used)");
    }
    else
    {
      Serial1.println("Dynamic IP address +++ (Current mode)");
    }

    Serial1.print("IP address  : ");
    Serial1.println(Ethernet.localIP()); // Print the device's IP address.
    Serial1.print("Subnet mask : ");
    Serial1.println(Ethernet.subnetMask()); // Print the device's subnet mask.
    Serial1.print("Gateway IP  : ");
    Serial1.println(Ethernet.gatewayIP()); // Print the gateway IP address.
    Serial1.print("DNS serv IP : ");
    Serial1.println(Ethernet.dnsServerIP()); // Print the DNS server IP address.
    //Serial1.println("Manual IP address +++");

    // start UDP
    /*
    console.println(MY_VERSION);    // "Surface Buoy v1.0.0"
    console.println(" + group_id : %d", config.group_id);
    console.println(" + node_id  : %d", config.node_id);
    console.println("   Control must be 0, Buoys 1..n");
    console.println("   note) P2P id == (node_id + 1) by default");
    console.println(" + lora_channel : %d (freq : %.1f MHz)", config.lora_channel, 
      LoraP2P::getChannelFrequency(config.lora_channel)/1000000.0);
    console.println(" + lora_power   : %d", config.lora_txpower);
    console.println(" + log_interval : %d", config.log_interval);
    */
  }
  else if (strcmp("save", cmd) == 0)
  {
    saveConfig(&config);
    console.println("Saving configuration done, reboot!!");
  }
  else if (strcmp("reboot", cmd) == 0)
  {
    console.println("Rebooting...");
    delay(100);
    SCB_AIRCR = 0x05FA0004;
  }
}

static int makeDefaultConfig(Config *pc)
{
  pc->pid = 0;
  pc->did = 1;               // 1..n
  pc->type = 1;               // 1..3
  pc->net_mode = 0;           // 0:DHCP, 1:MANUAL

  pc->net_ip[0] = 192;
  pc->net_ip[1] = 168;
  pc->net_ip[2] = 0;
  pc->net_ip[3] = 77;

  pc->net_gw[0] = 192;
  pc->net_gw[1] = 168;
  pc->net_gw[2] = 0;
  pc->net_gw[3] = 1;

  pc->net_sm[0] = 255;
  pc->net_sm[1] = 255;
  pc->net_sm[2] = 255;
  pc->net_sm[3] = 0;

  pc->port = 8888;

  pc->mac[0] = 0xDE;
  pc->mac[1] = 0xAD;
  pc->mac[2] = 0xBE;
  pc->mac[3] = 0xEF;
  pc->mac[4] = 0x1E;
  pc->mac[5] = 0xED;

  memcpy(pc->mark, CONFIG_MARK, 4);
  return 0;
}

static bool isValidConfig(Config *pc)
{
  if (memcmp(pc->mark, CONFIG_MARK, 4) != 0)
    return false;

  
  return true;
}


static int loadConfig(Config *pc)
{
  EEPROM.get(0, *pc);
  return 0;
}

static int saveConfig(Config *pc)
{
  EEPROM.put(0, *pc);
  return 0;
}

// parse text input to int
static int inputDataParseInt(char * buff)
{
  char inputDigit[10] = {0,0,0,0,0,0,0,0,0,0};
  int beginIndex = 7;
  while(buff[beginIndex] != 0xFF)
  {
    Serial.print(buff[beginIndex] - '0');
    inputDigit[beginIndex - 7] = buff[beginIndex];
    beginIndex++;
  }
  Serial.println(" ");
  int inputValue = atoi(inputDigit);
  Serial.println(inputValue);
  Serial.println(" ");

  return inputValue;
}

static void setBasicConfigValues()
{
  uint16_t pid[] = {config.pid};
  dgus.set_variable(0x1000, 1, pid);
  dgus.set_variable(0x2000, 1, pid);

  switch (config.type) 
  {
    case 1 : 
      dgus.set_text(0x1010, 5, "RPM  ");
      break;
    case 2 : 
      dgus.set_text(0x1010, 5, "ROT  ");
      break;
    case 3 : 
      dgus.set_text(0x1010, 5, "RA   ");
      break;
  }

  uint16_t did[] = {config.did};
  dgus.set_variable(0x2020, 1, did);
  dgus.set_variable(0x1020, 1, did);
  

  char buffer[10] = {0,0,0,0,0,0,0,0,0,0};
  sprintf(buffer, "%d", config.port);

  if (strlen(buffer) < 5) 
  {
    for (int i = strlen(buffer); i < 5; i++)
    {
      buffer[i] = ' ';
    }
    buffer[5] = '\0';
  }

  dgus.set_text(0x1030, 5, buffer);
  
  dgus.set_text(0x1040, 6, "AUTO  ");
}

static void setNetworkConfigValues()
{
  switch (config.net_mode) 
  {
    case 0 : 
      dgus.set_text(0x1050, 6, "DHCP  ");
      dgus.set_screen(PAGE_CONFIG_NETWORK_DHCP);
      break;
    case 1 : 
      dgus.set_text(0x1050, 6, "MANUAL");
      dgus.set_screen(PAGE_CONFIG_NETWORK_MANUAL);
      break;
  }

  Serial.print("config.net_ip : ");
  Serial.print(config.net_ip[0]); Serial.print(" ");
  Serial.print(config.net_ip[1]); Serial.print(" ");
  Serial.print(config.net_ip[2]); Serial.print(" ");
  Serial.print(config.net_ip[3]); Serial.println(" ");

  Serial.print("config.net_gw : ");
  Serial.print(config.net_gw[0]); Serial.print(" ");
  Serial.print(config.net_gw[1]); Serial.print(" ");
  Serial.print(config.net_gw[2]); Serial.print(" ");
  Serial.print(config.net_gw[3]); Serial.println(" ");

  Serial.print("config.net_sm : ");
  Serial.print(config.net_sm[0]); Serial.print(" ");
  Serial.print(config.net_sm[1]); Serial.print(" ");
  Serial.print(config.net_sm[2]); Serial.print(" ");
  Serial.print(config.net_sm[3]); Serial.println(" ");

  for (int i = 0; i < 4; i++)
  {
    char buffer[10] = {0,0,0,0};
    sprintf(buffer, "%d", config.net_ip[i]);

    if (strlen(buffer) < 3) 
    {
      for (int j = strlen(buffer); j < 3; j++)
      {
        buffer[j] = ' ';
      }
      buffer[3] = '\0';
    }

    dgus.set_text(0x1060 + (4 * i), 3, buffer);
  }

  for (int i = 0; i < 4; i++)
  {
    char buffer[10] = {0,0,0,0};
    sprintf(buffer, "%d", config.net_gw[i]);

    if (strlen(buffer) < 3) 
    {
      for (int j = strlen(buffer); j < 3; j++)
      {
        buffer[j] = ' ';
      }
      buffer[3] = '\0';
    }

    dgus.set_text(0x1080 + (4 * i), 3, buffer);
  }

  for (int i = 0; i < 4; i++)
  {
    char buffer[10] = {0,0,0,0};
    sprintf(buffer, "%d", config.net_sm[i]);

    if (strlen(buffer) < 3) 
    {
      for (int j = strlen(buffer); j < 3; j++)
      {
        buffer[j] = ' ';
      }
      buffer[3] = '\0';
    }

    dgus.set_text(0x1100 + (4 * i), 3, buffer);
  }

}