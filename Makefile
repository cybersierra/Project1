CC = gcc
CFLAGS += -Wall -Wextra -O2 -D_GNU_SOURCE
all: wish

wish: wish.c
	$(CC) $(CFLAGS) -o wish wish.c

clean:
	rm -f wish
