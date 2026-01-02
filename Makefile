CC = gcc
CFLAGS = -Wall -Wextra

.PHONY: all lib client server clean

all: lib client server

lib:
	$(CC) $(CFLAGS) -c lib/libmq.c -o libmq.o

client: lib
	$(CC) $(CFLAGS) client/main.c libmq.o -o sk2mqc

server:
	$(CC) $(CFLAGS) server/main.c -o sk2mqs

clean:
	rm -f sk2mqs sk2mqc libmq.o
