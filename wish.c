// Build: gcc -Wall -Wextra -O2 wish.c -o wish

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

// Error message
static void printError(void) {
    fputs("An error has occurred\n", stderr);
    fflush(stderr);
}

