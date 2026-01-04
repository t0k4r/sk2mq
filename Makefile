CC = gcc
CFLAGS = -Wall -Wextra

.PHONY: all lib client server clean

all: lib client server

lib:
	$(CC) $(CFLAGS) -fPIC -shared lib/libmq.c -o libmq.so

client: lib
	$(CC) $(CFLAGS) -L. -lmq -Wl,-rpath=./ client/main.c libmq.so -o sk2mqc

server:
	$(CC) $(CFLAGS) server/main.c -o sk2mqs

clean:
	rm -f sk2mqs sk2mqc libmq.so
