/* client.c - network multiplayer chatroom TCP */
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <stdio.h>
#include <ncurses.h>

#define NAMELENGTH 10

#define LINELEN 100
#define QLEN 6

/*
*****************************************************************************
** syntax:  ./client <host> <port>                                         **
*****************************************************************************
*/
int readLine(char* buffptr, int length);
int openSocket(char*, int);
int isValidName(char* name);

void draw_borders(WINDOW *screen) {
  int x, y, i;

  getmaxyx(screen, y, x);

  // 4 corners
  mvwprintw(screen, 0, 0, "+");
  mvwprintw(screen, y - 1, 0, "+");
  mvwprintw(screen, 0, x - 1, "+");
  mvwprintw(screen, y - 1, x - 1, "+");

  // sides
  for (i = 1; i < (y - 1); i++) {
    mvwprintw(screen, i, 0, "|");
    mvwprintw(screen, i, x - 1, "|");
  }

  // top and bottom
  for (i = 1; i < (x - 1); i++) {
    mvwprintw(screen, 0, i, "-");
    mvwprintw(screen, y - 1, i, "-");
  }
}

int main(int argc, char *argv[]) {
  int parent_x, parent_y, new_x, new_y;
  int obsSize = NAMELENGTH + 2;
  int input_size = 3;
  char* host;
  char buf[LINELEN] = {0}, *s = buf;
  int ch = 0;
  int sd; /* socket descriptor */
  uint16_t Pport;
  uint16_t msglen;
  uint16_t hostmsglen;
  if (argc != 3) {
    fprintf(stderr,"Error: Wrong number of arguments\n");
    fprintf(stderr,"usage:\n");
    fprintf(stderr,"%s address client_port \n", argv[0]);
    exit(EXIT_FAILURE);
  }
  host = argv[1];
  Pport = atoi(argv[2]);
  sd = openSocket(host, Pport);
  char valid = 'N';
  
  recv(sd, &valid, sizeof(char), 0);
  if (valid == 'Y') {
    do {
      char username[NAMELENGTH];
      do {
        dprintf(1, "Enter username: ");
        readLine(username, LINELEN);
      } while (!isValidName(username));
      uint8_t nameLen = strlen(username);
      send(sd, &nameLen, sizeof(uint8_t), MSG_WAITALL);
      send(sd, &username, nameLen, MSG_WAITALL);
      recv(sd, &valid, sizeof(valid), 0);
    } while (valid != 'Y');
    dprintf(1, "\nUsername accepted...\n\n");

  initscr();
  curs_set(FALSE);
  // set up initial windows
  getmaxyx(stdscr, parent_y, parent_x);
  WINDOW *output = newwin(parent_y - input_size, parent_x-obsSize, 0, 0);
  WINDOW *outbuffer = newwin(parent_y - input_size-1, parent_x-obsSize-2, 1,1);
  WINDOW *connected = newwin(parent_y - input_size-1, parent_x, 1,parent_x-obsSize+1);
  WINDOW *input = newwin(input_size, parent_x, parent_y - input_size, 0);

  draw_borders(output);
  draw_borders(input);
  keypad(input, TRUE);
  noecho();
  cbreak();
  scrollok(outbuffer, TRUE);
  timeout(1);
  fd_set readset;

  while(1) {
    FD_ZERO(&readset);
    FD_SET(0, &readset);
    FD_SET(sd, &readset);
    getmaxyx(stdscr, new_y, new_x);
    if (new_y != parent_y || new_x != parent_x) {
      parent_x = new_x;
      parent_y = new_y;
      wresize(output, new_y - input_size, new_x);
      wresize(outbuffer, new_y - input_size-2, new_x-2);
      wresize(input, input_size, new_x);
      mvwin(input, new_y - input_size, 0);
      wclear(stdscr);
      wclear(output);
      wclear(input);
      wclear(connected);
      draw_borders(output);
      draw_borders(input);
    }
    select(5, &readset, NULL,NULL, NULL);
    if (FD_ISSET(sd, &readset)) {
      char buff[LINELEN] = {0};
      msglen = 0;
      recv(sd, &msglen, sizeof(uint16_t), 0);
      hostmsglen = ntohs(msglen);
      recv(sd, &buff, hostmsglen, 0);
      buff[hostmsglen] = 0;
      if (buff[0] == 'U')
        wattron(outbuffer, A_BLINK);
      if (buff[0] == '*')
        wattron(outbuffer, A_BOLD);
      if (buff[0] == 'W') {
        wattron(outbuffer, A_BLINK | A_BOLD);
      }
      if (buff[0] == '%') {
        wclear(connected);
        wprintw(connected, "%s\n", buff+1);
      } else if (buff[hostmsglen -1] != '\n') {
        wprintw(outbuffer, "%s\n", buff);
      } else {
        wprintw(outbuffer, "%s", buff);
      }
      wattroff(outbuffer,A_BLINK | A_BOLD);
    }
    if (FD_ISSET(0, &readset)) {
      if ((ch = wgetch(input)) != ERR) {
        if (ch == '\n') {
          if (strlen(buf) > 0) {
            *s = 0;
            hostmsglen = strlen(buf);
            msglen = htons(hostmsglen);
            send(sd, &msglen,sizeof(uint16_t), MSG_DONTWAIT); 
            send(sd, &buf, hostmsglen, MSG_DONTWAIT);
            for (int i = 1; i < new_x-1; i++) {
              mvwprintw(input,1,i, " ");
            }
            s = buf;
            *s = 0;
          }
        } else if (ch == KEY_BACKSPACE) {
          if (s > buf) {
            *--s = 0;
            wprintw(input, "\b \b"); 
          } else {
            *s = 0;
          }
        } else if (s - buf < (long)sizeof buf - 1) {
          *s++ = ch;
          *s = 0;
          waddch(input, ch);
        }
      }
    mvwprintw(input, 1, 1, "%s", buf);
    }
    // draw to our windows
    wnoutrefresh(output);
    wnoutrefresh(outbuffer);
    wnoutrefresh(input);
    wnoutrefresh(connected);
    doupdate();
  }
  endwin();
}
  return 0;
}


/*
 * Function: openSocket
 * -------------------
 * Opens up a connection to the host
 *
 * *host: address of host to connect to
 * port:  port to connect to on host
 *
 * returns file descriptor of connected socket
 */
int openSocket(char* host, int port) {
  struct sockaddr_in sad; /* structure to hold an IP address */
  struct hostent *ptrh;
  int sd;
  int n;
  struct protoent *ptrp; /* pointer to a protocol table entry */

  memset((char *)&sad,0,sizeof(sad)); /* clear sockaddr structure */
  sad.sin_family = AF_INET; /* set family to Internet */

  ptrh = gethostbyname(host);
  if ( ptrh == NULL ) {
    fprintf(stderr,"Error: Invalid host: %s\n", host);
    exit(EXIT_FAILURE);
  }
  sad.sin_port = htons((u_short)port);
  memcpy(&sad.sin_addr, ptrh->h_addr, ptrh->h_length);

  /* Map TCP transport protocol name to protocol number. */
  if ( ((long int)(ptrp = getprotobyname("tcp"))) == 0) {
    fprintf(stderr, "Error: Cannot map \"tcp\" to protocol number");
    exit(EXIT_FAILURE);
  }

  /* Create a socket. */
  sd = socket(PF_INET, SOCK_STREAM, ptrp->p_proto);
  if (sd < 0) {
    fprintf(stderr, "Error: Socket creation failed\n");
    exit(EXIT_FAILURE);
  }

  if ((n = connect(sd, (struct sockaddr *)&sad, sizeof(sad))) < 0) {
    fprintf(stderr, "connect failed\n");
    exit(EXIT_FAILURE);
  }
  return sd;
}

/*
 * Function: isValidName
 * -------------------
 * Checks and returns the validity of entered name
 * Validity: length < NAMELENGTH and must be alphanumeric (no symbols)
 *
 * *name:  name to check
 *
  * returns if name is valid
 */
int isValidName(char* name) {
  int len = strlen(name);
  if (len == 0 || len > NAMELENGTH) {
    dprintf(1, "\nUsername is invalid: too long\n");
    return 0;
  }
  for (int i = 0; i < len; i++) {
    if (!isalpha(name[i]) && !isdigit(name[i]) && name[i] != '_'){
      dprintf(1, "\nUsername is invalid: only letters and digits\n");
      return 0;
    }
  }
  return 1;
}

/*
 * Function: readLine
 * -------------------
 * Read in a line and removes the newline
 *
 * *buffptr:  where to store the buffer
 * length:    desired size to store in the buffer
 * returns the length of the string
 */
int readLine(char* buffptr, int length) {
  if (fgets (buffptr, length, stdin) != buffptr)
    return -1;
  buffptr[strcspn(buffptr, "\n")] = 0;
  char c;
  /* eat up what's left over in the buffer */
  if (strlen(buffptr) == (length-1)) {
    while ((c = getchar()) != '\n' && c != 0);
  }
  return strlen(buffptr);
}
