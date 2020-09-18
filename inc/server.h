#include <stdint.h>  //for declaring uint8_t
#include <sys/select.h>

#define TIMEOUT 60
#define MAXCLIENT 255
#define NAMELENGTH 10
#define MSGLENGTH 1001

typedef struct client {
  uint8_t isActive;        //flag for if user is "active"
  uint8_t nameLen;         //length of username
  uint16_t socket;         //socket for client
  char* name;              //client name
  struct timeval timeout;  //timer for checkin timeouts
}client;
