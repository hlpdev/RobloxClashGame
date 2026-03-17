CC = gcc
CFLAGS = -Wall -Wextra -g -Iinclude -I/usr/include -I /usr/include/postgres
LDFLAGS = -lpq -lhiredis -lpthread -lcjson

SRC = $(shell find src -name '*.c')

all: server

server: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o server $(LDFLAGS)

compile_commands.json: $(SRC)
	bear -- make server

clean:
	rm -f server
