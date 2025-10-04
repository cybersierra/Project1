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

// Main function
int main(int arc, char *argv[]) {
    FILE *in = NULL;

    // Interactive
    if (argc == 1) {
        in = stdin;

    // Batch
    } else if (argc == 2) {
        in = fopen(argv[1], "r");
        if (in == NULL) {
            printError();
            exit(1);
        }
    } else {
        printError();
        exit(1);
    }

}