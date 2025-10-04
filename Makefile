CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu11

all: wish

wish: wish.c
	$(CC) $(CFLAGS) -o wish wish.c

clean:
	rm -f wish
