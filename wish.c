#include "wish.h"

// Error message
static void printError(void) {
    fputs("An error has occurred\n", stderr);
    fflush(stderr);

/* -------- utilities -------- */
static void err(void) {
    write(STDERR_FILENO, ERRMSG, sizeof(ERRMSG) - 1);
    
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
