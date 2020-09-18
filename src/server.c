/* server.c - network multiplayer chatroom TCP */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/time.h>
#include "../inc/server.h"

#define QLEN 6 /* size of request queue */

/*
*****************************************************************************
** syntax:  ./server <part port>                                           **
*****************************************************************************
 */
char isValidName(char*, int);
void startServer(int);
int openSocket(struct sockaddr_in,int, int);
void getConnectedUsers(int*, fd_set*, struct timeval);
void addUser(int);
void print();
void deleteUser(client*);
void newParticipant(int);
void sendToAllClients(char*);
void participantActions(int, fd_set);
void sendPrivate(char*, uint16_t, client*);
void sendListOfNames();

client *pset = NULL;       //array of participant clients
client *lowestTime = NULL; //pointer to client with lowest time
int numParts = 0;          //number of connected participants

int main(int argc, char **argv) {
  struct sockaddr_in sad; /* structure to hold server's address */
  int Psd; /* socket descriptors */
  uint16_t particpant_port; /* protocol port number */
  int optval = 1; /* boolean value when we set socket option */

  if( argc != 2 ) {
    fprintf(stderr,"Error: Wrong number of arguments\n");
    fprintf(stderr,"usage:\n");
    fprintf(stderr,"%s client_port \n", argv[0]);
    exit(EXIT_FAILURE);
  }
  particpant_port = atoi(argv[1]);
  if (particpant_port < 0 || particpant_port > 65535) {
    fprintf(stderr,"Error: Bad participant port number %d\n",particpant_port);
    exit(EXIT_FAILURE);
  }
  Psd = openSocket(sad, optval, particpant_port);
  startServer(Psd);
}

/*
 * Function: startServer
 * -------------------
 * Main function that controls the server
 *
 * Psd:  fd for client connections
  */
void startServer(int Psd) {
  int sock;
  struct sockaddr_in pad;
  pset = calloc(MAXCLIENT,sizeof(client));
  lowestTime = (client*)calloc(1, sizeof(client));  //only need one pointer
  int retval;                    //select return value
  fd_set rfds;                   //set of fds for select
  struct timeval elapsedTime;    //stores elapsed time since last Select
  struct timeval tv;             //timeval for select
  elapsedTime.tv_sec = 0;
  elapsedTime.tv_usec = 0;
  unsigned int alen = sizeof(pad);
  //keep the server alive
  while (1) {
    int maxfd = Psd;
    FD_ZERO(&rfds);
    FD_SET(Psd, &rfds);
    //handle timeouts and get currently connected clients
    getConnectedUsers(&maxfd, &rfds, elapsedTime);
    dprintf(1, "parts: %d\n", numParts);
    print();
    //run select with the lowest time
    tv = lowestTime->timeout;
    //If time is "0", there are no timeouts - block on select
    if (!timerisset(&tv)) {
      retval = select(maxfd + 1, &rfds, NULL, NULL, NULL);
    }
    else {
      retval = select(maxfd + 1, &rfds, NULL, NULL, &tv);
    }
    timersub(&lowestTime->timeout,&tv,&elapsedTime);
    if (retval == -1) {
      perror("select()");
      break;
    //timeouts are handled at the top of the while loop
    } else if (retval == 0) {
      continue;
    } else {
      if (FD_ISSET(Psd, &rfds)) {
        if ((sock=accept(Psd, (struct sockaddr *)&pad, &alen)) < 0) {
          fprintf(stderr, "Error: Accept failed\n");
          exit(EXIT_FAILURE);
        }
        dprintf(1, "New Participant\n");
        newParticipant(sock);
      } else {
        participantActions(sock, rfds);
      }
    }
  }
}

/*
 * Function: newParticipant
 * -------------------
 * Handles immediate connection to the server
 * disconnects socket if clients are full
 *
 * sock:  socket for new connection
 */
void newParticipant(int sock) {
  char valid;
  if (numParts >= MAXCLIENT) {
    valid = 'N';
    dprintf(1, "rejected a socket because full: %d-%d\n", numParts, MAXCLIENT);
    send(sock,&valid,sizeof(char),MSG_DONTWAIT);
    close(sock);
  } else {
    valid = 'Y';
    send(sock,&valid,sizeof(char),MSG_DONTWAIT);
    numParts++;
    addUser(sock);
  }
}

/*
 * Function: participantActions
 * -------------------
 * handles all things related to participant sockets
 *
 * sock:  socket that was flagged by select
 * rfds:  set of fds in select
 */
void participantActions(int sock, fd_set rfds) {
  char buf[MSGLENGTH+15];  //reuse the same buffer, +15 for padding messages
  int n;
  client *user = pset;
  for (int i = 0; i < MAXCLIENT; i++, user++) {
    if (user->socket < 1)
      continue;
    sock = user->socket;
    if (FD_ISSET(sock, &rfds)) {
      if (user->nameLen == 0) {
        uint8_t nameLen;
        n = recv(sock, &nameLen, sizeof(uint8_t), 0);
        // client disconnected, delete them
        if (n == 0) {
          numParts--;
          deleteUser(user);
          continue;
        }
        if (nameLen == 0) {
          dprintf(1, "name = 0\n");
          return;
        }
        user->nameLen = nameLen;
        n = recv(sock, &buf, user->nameLen, 0);
        buf[user->nameLen] = 0;
        char valid = isValidName(buf, 0);
        dprintf(1, "user: >%s< with length %d.  valid: %c\n", buf, user->nameLen, valid);
        send(sock,&valid,sizeof(char),MSG_WAITALL);
        switch (valid) {
          //Invalid name
          case 'I':
            user->nameLen = 0;
            break;
          //User name was taken, reset timer
          case 'T':
            user->nameLen = 0;
            user->timeout.tv_sec = TIMEOUT;
            user->timeout.tv_usec = 0;
            break;
          //username is good!
          case 'Y':
            user->timeout.tv_sec = 0;
            user->timeout.tv_usec = 0;
            user->name = strdup(buf);
            user->isActive = 1;
            dprintf(1, "User %s has joined, len: %d", user->name, user->nameLen);
            snprintf(buf, MSGLENGTH, "User %s has joined\n", user->name);
            sendToAllClients(buf);
            sendListOfNames();
            break;
        }
      } else {
        //We have a message instead of a name
        uint16_t msgLen;
        n = recv(sock, &msgLen, sizeof(uint16_t), 0);
        msgLen = ntohs(msgLen);
        dprintf(1, "new message, length: %d\n", msgLen);
        //if 0, user disconnected.
        //if message too big, disconnect the user ourselves
        if ((n == 0) || (msgLen > MSGLENGTH) || (msgLen >= MSGLENGTH)) {
          snprintf(buf, MSGLENGTH, "User %s has left", user->name);
          sendToAllClients(buf);
          numParts--;
          deleteUser(user);
          sendListOfNames();
        } else {
          memset(buf, 0, MSGLENGTH);
          n = recv(sock, &buf, msgLen, MSG_WAITALL);
          dprintf(1, "message: >%s<\n", buf);
          //user lied about the length of the message, don't send it
          if (n != msgLen) {
            dprintf(1, "message lengths do not match\n");
            return;
          }
          if (buf[0] == '@') {
            //private message
            sendPrivate(buf,msgLen, user);
          } else if ((buf[0] == '\\' || buf[0] == '/') && buf[1] == 'm' && buf[2] == 'e') {
            //action
            char msg[MSGLENGTH+15] = {0};
            snprintf(msg, MSGLENGTH, "*%s%s", user->name, buf+3);
            dprintf(1, "message: >%s<\n", buf+3);
            msg[strcspn(msg, "\n")] = 0;
            sendToAllClients(msg);
          } else {
            //regular message
            char msg[MSGLENGTH+20] = {0};
            int pad = 10 - (user->nameLen);
            sprintf(msg, "%c%*c%s: %s", '>', pad,' ', user->name, buf);
            msg[strcspn(msg, "\n")] = 0;
            sendToAllClients(msg);
          }
        }
      }
    }
  }
}

/*
 * Function: addUser
 * -------------------
 * register a newly connected user to an array
 *
 * socket:  socket associated with client
 * isPart:  true if client is participant
 */
void addUser(int socket){
  client *puser = NULL;
    puser = pset;
  for (int i = 0; i < MAXCLIENT; i++, puser++) {
    if (puser->socket == 0) {
      puser->isActive = 0;
      puser->socket = socket;
      puser->nameLen = 0;
      puser->timeout.tv_sec = TIMEOUT;
      puser->timeout.tv_usec = 0;
      return;
    }
  }
}

/*
 * Function: deleteUser
 * -------------------
 * remove a client from the array
 *
 * *puser:  pointer to the user
 */
void deleteUser(client *puser){
  close(puser->socket);
  puser->socket = 0;
  puser->nameLen = 0;
  timerclear(&puser->timeout);
  puser->name = 0;
  free(puser->name);
  puser->isActive = 0;
  return;
}

/*
 * Function: isValidName
 * -------------------
 * determines if username is valid
 *
 * name:    name to associate with new client
 * sock:    socket associated with the client
 * isPart:  True if the client is a participant
 *
 * returns character value based on validity of the name
 */
char isValidName(char* name, int sock) {
  int len = strlen(name);
  if (len == 0 || len > NAMELENGTH) {
      return 'I';
  }
  //alphabet, numbers, and underscores are the only things allowed
  for (int i = 0; i < len; i++) {
    if (!isalpha(name[i]) && !isdigit(name[i]) && name[i] != '_'){
        return 'I';
    }
  }
  //check if name allready exists as a participant
  //participants may not have the same name
  client *puser = pset;
  for (int i = 0; i < MAXCLIENT; i++, puser++) {
    if (puser->isActive) {
      if (strcmp(puser->name,name) == 0) {
          return 'T';
      }
    }
  }
  return 'Y';
}

/*
 * Function: sendListOfNames
 * -------------------
 * send the list of connected users to all clients
 *
 */
void sendListOfNames(){
  client *puser = pset;
  int totalLength = 1;
  for (int i = 0; i < MAXCLIENT; i++, puser++) {
    if (puser->isActive) {
      totalLength += strlen(puser->name) + 1;
    }
  }
  char *nameList = malloc(totalLength);
  puser = pset;
  strcpy(nameList, "%\n");
  int pos = 2;
  for (int i = 0; i < MAXCLIENT; i++, puser++) {
    if (puser->isActive) {
      pos += sprintf(&nameList[pos-1], " %s\n", puser->name);
    }
  }
  sendToAllClients(nameList);
  free(nameList);
}

/*
 * Function: sendToAllClients
 * -------------------
 * send a message to all connected clients
 *
 * msg:  the message to send to the clients
 */
void sendToAllClients(char* msg) {
  client *client = pset;
  uint16_t len = strlen(msg);
  uint16_t netlen = htons(len);
  for (int i = 0; i < MAXCLIENT; i++, client++) {
    if (client->socket > 0) {
      send(client->socket, &netlen, sizeof(uint16_t), MSG_NOSIGNAL | MSG_DONTWAIT);
      send(client->socket, msg, len, MSG_NOSIGNAL | MSG_DONTWAIT);
    }
  }
}

/*
 * Function: sendPrivate
 * -------------------
 * send a private message to a client
 *
 * buf:     message from to be modified
 * msgLen:  length of original message
 * *user:   pointer to user sending the message
 */
void sendPrivate(char* buf, uint16_t msgLen, client* user) {
    uint16_t netLen;
    char dest[NAMELENGTH+1];
    char* msg = buf;
    //username is the first
    while (*msg != ' ' && *msg != 0) {
      msg++;
    }
    *(msg++) = 0;
    strncpy(dest, buf+1, msgLen);
    dest[msgLen] = 0;
    char fmsg[MSGLENGTH+15] = {0};
    int pad = 11 - (user->nameLen);
    sprintf(fmsg, "%c%*c%s: %s", '*', pad,' ', user->name, msg);
    msgLen = strlen(fmsg);
    fmsg[msgLen] = 0;
    netLen = htons(msgLen);
    client *client = pset;
    for (int i = 0; i < MAXCLIENT; i++, client++) {
      if (client->isActive) {
        if (strcmp(dest, client->name) == 0) {
            send(client->socket, &netLen, sizeof(uint16_t), MSG_DONTWAIT);
            send(client->socket, fmsg, msgLen, MSG_DONTWAIT);
          if (client->socket != user->socket) {
            send(user->socket, &netLen, sizeof(uint16_t), MSG_DONTWAIT);
            send(user->socket, fmsg, msgLen, MSG_DONTWAIT);
          }
          return;
        }
      }
    }
    snprintf(buf, MSGLENGTH, "Warning: user %s doesn't exist...", dest);
    msgLen = strlen(buf);
    netLen = htons(msgLen);
    send(user->socket, &netLen, sizeof(uint16_t), MSG_DONTWAIT);
    send(user->socket, buf, msgLen, MSG_DONTWAIT);
}

/*
 * Function: getConnectedUsers
 * -------------------
 * Adjust all the timer values for connected clients
 * remove any clients that have timed out
 * adjust pointer to the lowest time
 *
 * *maxfd:    pointer to the current maxfd
 * *rfds:     pointer to the set of fds
 * elapTime:  elapsted time since last Select call
 * isPart:    True if we're adjusting participants
 */
void getConnectedUsers(int *maxfd, fd_set *rfds, struct timeval elapTime) {
  client *user = NULL;
    user = pset;
  struct timeval lowestTimer;
  lowestTimer.tv_sec = TIMEOUT+1; //+1 second garuntees that a timer will always get set
  lowestTimer.tv_usec = 0;
  for (int i=0; i< MAXCLIENT; i++, user++) {
    //only adjust timers on non-username clients
    if (user->socket > 0) {
      if(!user->isActive) {
        timersub(&user->timeout,&elapTime,&user->timeout);
        //if the timer is now 0, remove them from the array
        if (!timerisset(&user->timeout)) {
          numParts--;
          deleteUser(user);
          continue;
        } else if (timercmp(&user->timeout,&lowestTimer,<)) {
          lowestTimer = user->timeout;
          lowestTime = user;
        }
      }
      FD_SET(user->socket, rfds);
      *maxfd = (*maxfd > user->socket) ? *maxfd : user->socket;
    }
  }
}



/*
 * Function: print
 * -------------------
 * helper function while developing to display connected participants
 *
 * returns
 */
void print() {
  client *user = pset;
  dprintf(2, "***********\n");
  dprintf(1, "parts: %d\n", numParts);
  for (int i=0; i< MAXCLIENT; i++, user++) {
    if (user->socket > 0) {
      if (user->isActive)
        dprintf(2, "  %s\n", user->name);
      dprintf(2, "    psock:%d\n", user->socket);
      dprintf(2, "    nlen :%d\n", user->nameLen);
      dprintf(2, "    time :%ld:%ld\n\n", user->timeout.tv_sec, user->timeout.tv_usec);
    }
  }
  dprintf(2, "***********\n");
}

/*
 * Function: openSocket
 * -------------------
 * opens a socket to the port
 *
 * sad:     server address
 * optval:  socket stuff
 * port:    port to open up on the server
 *
 * returns socket fd
 */
int openSocket(struct sockaddr_in sad,int optval, int port) {
  int sd;
  struct protoent *ptrp; /* pointer to a protocol table entry */
  memset((char *)&sad,0,sizeof(sad)); /* clear sockaddr structure */
  sad.sin_family = AF_INET; /* set family to Internet */
  sad.sin_addr.s_addr = INADDR_ANY; /* set the local IP address */
  sad.sin_port = htons((u_short)port);

  /* Map TCP transport protocol name to protocol number */
  if ( ((long int)(ptrp = getprotobyname("tcp"))) == 0) {
    fprintf(stderr, "Error: Cannot map \"tcp\" to protocol number");
    exit(EXIT_FAILURE);
  }

  /* Create a socket */
  sd = socket(PF_INET, SOCK_STREAM, ptrp->p_proto);
  if (sd < 0) {
    fprintf(stderr, "Error: Socket creation failed\n");
    exit(EXIT_FAILURE);
  }

  /* Allow reuse of port - avoid "Bind failed" issues */
  if( setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0 ) {
    fprintf(stderr, "Error Setting socket option failed\n");

    exit(EXIT_FAILURE);
  }

  /* Bind a local address to the socket */
  if (bind(sd, (struct sockaddr *)&sad, sizeof(sad)) < 0) {
    fprintf(stderr,"Error: Bind failed\n");
    exit(EXIT_FAILURE);
  }

  /* Specify size of request queue */
  if (listen(sd, QLEN) < 0) {
    fprintf(stderr,"Error: Listen failed\n");
    exit(EXIT_FAILURE);
  }

  return sd;
}
