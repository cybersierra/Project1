#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   // for fork(), execv(), access(), chdir()
#include <sys/wait.h> // for waitpid()
#include <fcntl.h>    // for open(), O_CREAT, O_WRONLY, O_TRUNC
#include <errno.h>    // for errno values

// The *only* allowed error message per spec
static const char ERRMSG[] = "An error has occurred\n";

/* ===========================================================
   ==========   BASIC ERROR HANDLER + GLOBAL PATH   ===========
   =========================================================== */

// Prints the official error message to STDERR
static void err(void) {
    write(STDERR_FILENO, ERRMSG, sizeof(ERRMSG) - 1);
}

// Global variable storing the search PATH as a NULL-terminated array
// Example: ["/bin", "/usr/bin", NULL]
static char **g_path = NULL;

/* Initialize default path to contain just "/bin" */
static void path_init(void) {
    g_path = malloc(2 * sizeof(char*));
    if (!g_path) { err(); exit(1); }
    g_path[0] = strdup("/bin");
    g_path[1] = NULL;
}

/* Frees all memory associated with the path array */
static void path_free(void) {
    if (!g_path) return;
    for (size_t i = 0; g_path[i]; i++) free(g_path[i]);
    free(g_path);
    g_path = NULL;
}

/* ===========================================================
   ==========        STRING PARSING HELPERS          =========
   =========================================================== */

/*
 * split_tokens():
 * Splits a string `s` into tokens separated by any of the given `delims`
 * (like " " or "&" or "\t"). Uses strsep() so it handles multiple
 * consecutive delimiters gracefully.
 * Returns a NULL-terminated array of strdup’d strings.
 * Caller must free with free_argv().
 */
static char **split_tokens(char *s, const char *delims) {
    size_t cap = 8, n = 0;
    char **out = malloc(cap * sizeof(char*));
    if (!out) return NULL;
    char *tok;

    while ((tok = strsep(&s, delims)) != NULL) {
        // skip empty pieces
        if (*tok == '\0') continue;

        // trim whitespace on both ends
        while (*tok == ' ' || *tok == '\t') tok++;
        char *end = tok + strlen(tok);
        while (end > tok && (end[-1] == ' ' || end[-1] == '\t')) end--;
        *end = '\0';

        if (*tok == '\0') continue;

        // expand storage if needed
        if (n + 1 >= cap) {
            cap *= 2;
            out = realloc(out, cap * sizeof(char*));
            if (!out) return NULL;
        }

        out[n++] = strdup(tok);
    }

    // terminate array with NULL
    if (n + 1 >= cap) {
        cap += 1;
        out = realloc(out, cap * sizeof(char*));
        if (!out) return NULL;
    }
    out[n] = NULL;
    return out;
}

/* Frees token arrays created by split_tokens() */
static void free_argv(char **argv) {
    if (!argv) return;
    for (size_t i=0; argv[i]; i++) free(argv[i]);
    free(argv);
}

/* ===========================================================
   ==========        PATH SEARCH FOR EXECUTABLES     =========
   =========================================================== */

/*
 * resolve_exec():
 * Given a command name (like "ls"), check each directory in g_path.
 * Build a full path like "/bin/ls" and test if it exists & is executable
 * via access(path, X_OK).
 * Returns malloc'd string with the full path, or NULL if not found.
 */
static char *resolve_exec(const char *cmd) {
    if (!g_path || !g_path[0]) return NULL;
    for (size_t i=0; g_path[i]; i++) {
        size_t need = strlen(g_path[i]) + 1 + strlen(cmd) + 1;
        char *p = malloc(need);
        if (!p) { err(); exit(1); }
        snprintf(p, need, "%s/%s", g_path[i], cmd);
        if (access(p, X_OK) == 0) return p;  // found a valid executable
        free(p);
    }
    return NULL;
}

/* ===========================================================
   ==========   PARSE COMMAND + HANDLE REDIRECTION   ==========
   =========================================================== */

/*
 * parse_cmd_with_redir():
 * Takes a command line segment (like "ls -l > out.txt").
 * Splits out the filename following '>' (if any) and returns
 * an argv[] for the command itself.
 *
 * - Only ONE '>' allowed.
 * - Exactly ONE filename allowed after '>'.
 * - Returns NULL if syntax is invalid.
 */
static char **parse_cmd_with_redir(char *segment, char **redir_path) {
    *redir_path = NULL;

    // count number of '>' symbols
    int gt_count = 0;
    for (char *c = segment; *c; c++) if (*c == '>') gt_count++;
    if (gt_count > 1) { err(); return NULL; }

    char *left = segment;
    char *right = NULL;

    if (gt_count == 1) {
        // split at the first '>'
        right = strchr(segment, '>');
        *right = '\0';
        right++;

        // tokens after '>' should give exactly one filename
        char **r = split_tokens(right, " \t");
        if (!r || !r[0] || r[1]) {
            err();
            free_argv(r);
            return NULL;
        }
        *redir_path = strdup(r[0]);
        free_argv(r);
    }

    // split command part into argv[]
    char **argv = split_tokens(left, " \t");
    if (!argv || !argv[0]) {
        free_argv(argv);
        if (*redir_path) { free(*redir_path); *redir_path=NULL; }
        return NULL;
    }
    return argv;
}

/* ===========================================================
   ==========        BUILT-IN COMMAND HANDLER         =========
   =========================================================== */

/*
 * handle_builtin():
 * Checks if argv[0] is one of the built-in commands (exit, cd, path).
 * Executes them directly inside the shell (no forking).
 * Returns 1 if handled, 0 otherwise.
 */
static int handle_builtin(char **argv) {
    if (!argv || !argv[0]) return 0;

    // ======= exit =======
    if (strcmp(argv[0], "exit") == 0) {
        if (argv[1] != NULL) { err(); return 1; } // exit takes no args
        path_free();
        exit(0);
    }

    // ======= cd =======
    if (strcmp(argv[0], "cd") == 0) {
        if (!argv[1] || argv[2]) { err(); return 1; } // exactly one arg
        if (chdir(argv[1]) != 0) err();
        return 1;
    }

    // ======= path =======
    if (strcmp(argv[0], "path") == 0) {
        // clear old path
        path_free();

        // count new dirs
        size_t count = 0;
        while (argv[1+count]) count++;

        // rebuild global path
        g_path = malloc((count + 1) * sizeof(char*));
        if (!g_path) { err(); exit(1); }
        for (size_t i=0; i<count; i++)
            g_path[i] = strdup(argv[1+i]);
        g_path[count] = NULL;
        return 1;
    }

    return 0; // not a built-in command
}

// ========== external command handler ==========

// run_external():
// Launches an external program (like /bin/ls).
// Handles redirection (if redir_path != NULL).
// Uses fork() + execv() + wait().
// Parent continues after child creation.

static pid_t run_external(char **argv, const char *redir_path) {
    // sanity check
    if(!argv || !argv[0]) {
        err();
        return -1;
    }
    
    char *prog = NULL;

    // if the user provides a path (contains '/'), use it directly; otherwise search PATH
    if (strchr(argv[0], '/')) {
        if(access(argv[0], X_OK) != 0) {
            err();
            return -1;
        }
        prog = strdup(argv[0]);
        if (!prog){
            err();
            return -1;
        }
    }
    else{
        // search PATH and fail if not found
        if (!g_path || !g_path[0]){
            err();
            return -1;
        }

        prog = resolve_exec(argv[0]);
        if (!prog){
            err();
            return -1;
        }
    }
    

    /*
    // try to find the absolute path from what the user entered, if it doesn't work then print error
    char *prog = resolve_exec(argv[0]);
    if (!prog) {
        err();
        return -1;
    }
    */

    // assuming that the program exists, fork a child process
    pid_t pid = fork();
    // if the fork fails, print error and free the memory allocated for the program
    if (pid < 0) {
        err();
        free(prog);
        return -1;
    }

    // now we are in the child process
    if (pid == 0) {
        // if output redirection is needed
        if (redir_path) {
            // create the file (if it doesn't exist), open it for writing, and truncate if it already exists
            int fd = open(redir_path, O_CREAT|O_WRONLY|O_TRUNC, 0666);
            
            // if the file cannot be opened, print error and exit
            if (fd < 0) { 
                err(); 
                _exit(1); 
            }

            // redirect stdout and stderr to file
            if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
                err();
                _exit(1);
            }

            // close the original file descriptor
            close(fd);
        }

        // replace the child process with the new program
        execv(prog, argv);

        // if the execv fails, print error and exit child
        err();
        _exit(1);
    } else {
        // parent process: just free the program path
        free(prog);

        // return the child's PID to the caller
        return pid; 
    }
}


// ========== main loop ==========
int main(int argc, char *argv[]) {
    // 'in' is where we are reading commands from, either stdin or a file
    FILE *in = NULL;
    // interactive is 1 if we should show a prompt, 0 if running in batch mode
    int interactive = 1;

    // determine input source

    // if there are no arguments, read from stdin (interactive mode)
    if (argc == 1) {
        in = stdin;
        interactive = 1;
    } 
    // if there is one argument, read from the specified file (batch mode)
    else if (argc == 2) {
        in = fopen(argv[1], "r");
        
        // if the file cannot be opened, print error and exit
        if (!in) { 
            err(); 
            exit(1); 
        }
        interactive = 0;
    } 
    // if there is more than one argument, error and exit
    else {
        err();
        exit(1);
    }

    // initialize PATH list to ["/bin", NULL]
    path_init();

    // buffer for getline()
    char *line = NULL;
    size_t cap = 0;

    // main shell loop
    while (1) {
        // show prompt only in interactive mode
        if (interactive) {
            printf("wish> ");
            fflush(stdout);
        }

        // read one line of input at a time until EOF and then exit
        ssize_t n = getline(&line, &cap, in);
        if (n == -1) break;

        // strip trailing newlines
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';

        // split by '&' for parallel commands
        char **segments = split_tokens(line, "&");
        
        // if split_tokens fails, print error and continue to next line
        if (!segments) {
            err();
            continue;
        }

        // collect child PIDs to wait for them all later
        size_t started = 0;

        // arbitrary limit (enough for tests)
        pid_t kids[256]; 

        // process each command segment separately
        for (size_t i=0; segments[i]; i++) {
            char *redir = NULL;
            // parse_cmd_with_redir makes sure that we don't use more than one >, only one filename after >, and makes cmd = argv[]
            char **cmd = parse_cmd_with_redir(segments[i], &redir);
            
            // if parsing fails, print error and skip this segment
            if (!cmd) { 
                free(redir); 
                continue; 
            }

            // skip empty commands (like "ls && pwd")
            if (!cmd[0]) { 
                free_argv(cmd); 
                free(redir); 
                continue; 
            }

            // check for built-ins like exit, cd, or PATH (execute immediately)
            if (handle_builtin(cmd)) {
                free_argv(cmd);
                free(redir);
                continue;
            }

            /*
            // non-built-in: run external command
            char *prog = resolve_exec(cmd[0]);

            // if the exe doesn't exist in any of the paths, print error and skip this segment
            if (!prog) {
                err();
                free_argv(cmd);
                free(redir);
                continue;
            }
            */

            /* ~~~~~ This block is redundant ~~~~~~

            // fork a child process for this command
            pid_t pid = fork();

            // if we fail to fork, print error and skip this segment
            if (pid < 0) {
                err();
            }

            // if we are in the child process, do redirection and execv
            else if (pid == 0) {
                // this whole thing is almost exactly like what we did in run_external()
                if (redir) {
                    // create the file (if it doesn't exist), open it for writing, and truncate if it already exists
                    int fd = open(redir, O_CREAT|O_WRONLY|O_TRUNC, 0666);

                    // if the file cannot be opened, print error and exit
                    if (fd < 0) { 
                        err(); 
                        _exit(1); 
                    }

                    // redirect stdout and stderr to file
                    if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) { 
                        err(); 
                        _exit(1); 
                    }

                    // close the original file descriptor
                    close(fd);
                }

                // replace the child process with the new program
                execv(prog, cmd);

                // if execv fails, print error and exit child
                err();
                _exit(1);
            } else {
                // save child PID in array to wait for it later
                if (started < sizeof(kids)/sizeof(kids[0])) {
                    kids[started++] = pid;
                }
            }

            // free memory allocated for this command segment
            free(prog);
            free_argv(cmd);
            // free redirection path if any
            free(redir);
            */

            // run the external command (handles forking internally)
            pid_t pid = run_external(cmd, redir);

            if (pid > 0 && started < sizeof(kids)/sizeof(kids[0])) {
                // save child PID in array to wait for it later
                kids[started++] = pid;
            }

            free_argv(cmd);
            free(redir);
        }

        // wait for all child processes to finish
        for (size_t i=0; i<started; i++) {
            // waitpid can be interrupted by signals, so we loop until it succeeds
            while (waitpid(kids[i], NULL, 0) < 0 && errno == EINTR) {
            }
        }

        // free memory allocated for command segments
        free_argv(segments);
    }

    // cleanup on EOF
    free(line);
    path_free();
    return 0;
}