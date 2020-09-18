CC     = gcc
CFLAGS = -g -Wall -pedantic
LIBS   = -lncurses
INCDIR = inc
SRCDIR = src
CLIENT_SRC   := $(SRCDIR)/client.c
SERVER_SRC   := $(SRCDIR)/server.c

.PHONY: client server

all: client server

client:
	@$(CC) $(CFLAGS) -o client $(CLIENT_SRC) $(LIBS)

server:
	@$(CC) $(CFLAGS) -o server $(SERVER_SRC) $(LIBS)

clean:
	@$(RM) server
	@$(RM) client
