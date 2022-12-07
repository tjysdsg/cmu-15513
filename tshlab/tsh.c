/**
 * @file tsh.c
 * @brief A tiny shell program with job control
 *
 * Features:
 * - Several builtin commands
 * - Run programs as child process(s)
 * - I/O redirection of commands/programs
 *
 * Builtin commands:
 * - The quit command terminates the shell.
 * - The jobs command lists all background jobs.
 * - The bg job command resumes job by sending it a SIGCONT signal, and then
 *   runs it in the background. The job argument can be either a PID or a JID.
 * - The fg job command resumes job by sending it a SIGCONT signal, and then
 *   runs it in the foreground. The job argument can be either a PID or a JID.
 *
 * tsh supports running programs the same way as a regular
 * Bourne shell, except the programs' full path must be specified explicitly.
 * It allows running a program in the foreground or background.
 *
 * It also allows I/O redirection as following:
 * tsh> /bin/cat < foo > bar
 *
 * tsh handles signals from the user by using signal handlers. These handlers
 * are async-safe and do not affect errno.
 * It also monitors the status of child processes using a SIGCHLD handler.
 * When a child is terminated, either by quitting or external signals,
 * tsh reaps it so that no zombie process is left.
 *
 * @author Jiyang Tang <jiyangta@andrew.cmu.edu>
 */

#include "csapp.h"
#include "tsh_helper.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * If DEBUG is defined, enable contracts and printing on dbg_printf.
 */
#ifdef DEBUG
/* When debugging is enabled, these form aliases to useful functions */
#define dbg_printf(...) sio_eprintf(__VA_ARGS__)
#define dbg_requires(...) assert(__VA_ARGS__)
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_ensures(...) assert(__VA_ARGS__)
#else
/* When debugging is disabled, no code gets generated for these */
#define dbg_printf(...)
#define dbg_requires(...)
#define dbg_assert(...)
#define dbg_ensures(...)
#endif

/*
 * Global variables
 */

/**
 * pid of the recently terminated child process
 */
volatile sig_atomic_t g_sigchld_pid = 0;

/**
 * Stores the file descriptor to STDIN
 */
int g_stdin_fileno = -1;

/**
 * Stores the file descriptor to STDOUT
 */
int g_stdout_fileno = -1;

/**
 * Mask all signals
 *
 * @see init_sig_masks
 */
sigset_t g_mask_all;

/**
 * Mask SIGCHLD
 *
 * @see init_sig_masks
 */
sigset_t g_mask_chld_int_stp;

/**
 * Empty mask
 *
 * @see init_sig_masks
 */
sigset_t g_mask_empty;

/* Function prototypes */
void eval(const char *cmdline);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);
void sigquit_handler(int sig);
void cleanup(void);

static void init_sig_masks(void);
static void eval_quit(void);
static void eval_jobs(void);
static void eval_builtin(struct cmdline_tokens token);
static void eval_bg(const char *arg);
static void eval_fg(const char *arg);
static void wait_fg_job(void);
static bool redirect_io(const char *infile, const char *outfile);
static int open_file(const char *filename, const char *mode);

/**
 * Helper macros for guarding signals before calling async-unsafe routines
 *
 * @see sig_guard_end
 */
#define sig_guard_start()                                                      \
    sigset_t __prev_mask;                                                      \
    sigprocmask(SIG_BLOCK, &g_mask_all, &__prev_mask);

/**
 * Helper macros for guarding signals before calling async-unsafe routines
 * @see sig_guard_start
 */
#define sig_guard_end() sigprocmask(SIG_SETMASK, &__prev_mask, NULL);

/**
 * The main function initialize necessary variables and settings,
 * parse the command line options, initialize signal handlers, and call eval()
 * to run commands/programs.
 *
 * @see eval
 */
int main(int argc, char **argv) {
    init_sig_masks();

    int c;
    char cmdline[MAXLINE_TSH]; // Cmdline for fgets
    bool emit_prompt = true;   // Emit prompt (default)

    // Redirect stderr to stdout (so that driver will get all output
    // on the pipe connected to stdout)
#ifndef DEBUG
    if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
        perror("dup2 error");
        exit(1);
    }
#endif

    // backup file descriptor to STDIN and STDOUT
    g_stdin_fileno = dup(STDIN_FILENO);
    g_stdout_fileno = dup(STDOUT_FILENO);

    // Parse the command line
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h': // Prints help message
            usage();
            break;
        case 'v': // Emits additional diagnostic info
            verbose = true;
            break;
        case 'p': // Disables prompt printing
            emit_prompt = false;
            break;
        default:
            usage();
        }
    }

    // Create environment variable
    if (putenv(strdup("MY_ENV=42")) < 0) {
        perror("putenv error");
        exit(1);
    }

    // Set buffering mode of stdout to line buffering.
    // This prevents lines from being printed in the wrong order.
    if (setvbuf(stdout, NULL, _IOLBF, 0) < 0) {
        perror("setvbuf error");
        exit(1);
    }

    // Initialize the job list
    init_job_list();

    // Register a function to clean up the job list on program termination.
    // The function may not run in the case of abnormal termination (e.g. when
    // using exit or terminating due to a signal handler), so in those cases,
    // we trust that the OS will clean up any remaining resources.
    if (atexit(cleanup) < 0) {
        perror("atexit error");
        exit(1);
    }

    // Install the signal handlers
    Signal(SIGINT, sigint_handler);   // Handles Ctrl-C
    Signal(SIGTSTP, sigtstp_handler); // Handles Ctrl-Z
    Signal(SIGCHLD, sigchld_handler); // Handles terminated or stopped child

    Signal(SIGTTIN, SIG_IGN);
    Signal(SIGTTOU, SIG_IGN);

    Signal(SIGQUIT, sigquit_handler);

    // Execute the shell's read/eval loop
    while (true) {
        if (emit_prompt) {
            printf("%s", prompt);

            // We must flush stdout since we are not printing a full line.
            fflush(stdout);
        }

        if ((fgets(cmdline, MAXLINE_TSH, stdin) == NULL) && ferror(stdin)) {
            perror("fgets error");
            exit(1);
        }

        if (feof(stdin)) {
            // End of file (Ctrl-D)
            printf("\n");
            return 0;
        }

        // Remove any trailing newline
        char *newline = strchr(cmdline, '\n');
        if (newline != NULL) {
            *newline = '\0';
        }

        // Evaluate the command line
        eval(cmdline);
    }

    return -1; // control never reaches here
}

/**
 * Evaluate a command. The command is either a builtin command or a program
 * to run (with relevant args).
 *
 * It makes sure there is no race condition between the main process and the
 * child process using correct signal blocking.
 *
 * It update the job list if necessary.
 *
 * @see eval_builtin
 */
void eval(const char *cmdline) {
    dbg_printf("\ncmd: %s\n===========\n", cmdline);

    parseline_return parse_result;
    struct cmdline_tokens token;

    // Parse command line
    parse_result = parseline(cmdline, &token);

    if (parse_result == PARSELINE_ERROR || parse_result == PARSELINE_EMPTY) {
        return;
    }
    dbg_assert(token.argc);

    /*
     * Run builtin commands
     */
    if (token.builtin != BUILTIN_NONE) { // builtin command
        if (!redirect_io(token.infile, token.outfile)) {
            return;
        }

        eval_builtin(token);

        // reset
        if (!redirect_io(NULL, NULL)) {
            sio_eprintf("Unable to reset redirection of STDIN/STDOUT\n");
            _exit(1);
        }
        return;
    }

    /*
     * Run programs
     */
    job_state state = parse_result == PARSELINE_FG ? FG : BG;

    // block SIGCHLD
    sigset_t prev_mask;
    sigprocmask(SIG_BLOCK, &g_mask_chld_int_stp, &prev_mask);
    dbg_assert(!sigismember(&prev_mask, SIGCHLD));
    dbg_assert(!sigismember(&prev_mask, SIGINT));
    dbg_assert(!sigismember(&prev_mask, SIGTSTP));

    // spawn child process to run the program
    int pid = fork();
    if (0 == pid) { // child
        /*
         * Set the gid to child process' pid
         * This prevents the child processes to be killed/stopped/interrupted
         * when the shell gets a signal
         */
        if (setpgid(0, 0)) {
            perror("eval: setpgid failed");
            _exit(1);
        }

        if (!redirect_io(token.infile, token.outfile)) {
            _exit(1);
        }

        // unblock SIGCHLD before calling execve
        // since the child process inherits signal masks
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);

        if (execve(token.argv[0], token.argv, environ)) {
            perror(cmdline);
            _exit(1);
        }
        _exit(0);
    }

    // block all signals when adding job to the list
    sigprocmask(SIG_BLOCK, &g_mask_all, NULL);
    jid_t jid = add_job(pid, state, cmdline);
    dbg_printf("Added job to job list\n");

    // wait for foreground job to complete
    if (parse_result == PARSELINE_FG) {
        wait_fg_job();
    } else if (parse_result == PARSELINE_BG) {
        sio_printf("[%d] (%d) %s\n", jid, pid, cmdline);
    }

    // remember to reset signal masks
    sigprocmask(SIG_SETMASK, &prev_mask, NULL);
}

/**
 * Wait until the foreground job finishes
 */
static void wait_fg_job() {
    while (fg_job()) {
        dbg_printf("wait fg (1): g_sigchld_pid=%d\n", g_sigchld_pid);
        sigsuspend(&g_mask_empty);
        dbg_printf("wait fg (2): g_sigchld_pid=%d\n", g_sigchld_pid);
    }
    dbg_printf("wait_fg_job ended\n");
}

/*****************
 * Signal handlers
 *****************/

/**
 * Handles SIGCHLD signal
 *
 * - Reaps all dead child processes.
 * - Update the job list if the child process is stopped.
 * - Prints status of the child.
 *
 * @param sig Signal number
 */
void sigchld_handler(int sig) {
    int olderrno = errno;
    sig_guard_start();

    // use a while loop to reap all children
    int pid = 0;
    int status = 0;
    // WUNTRACED for stopped (SIGTSTP) children
    // WNOHANG for immediate return if no children have exited
    while ((pid = waitpid(-1, &status, WUNTRACED | WNOHANG)) > 0) {
        g_sigchld_pid = pid;
        jid_t jid = job_from_pid(g_sigchld_pid);
        dbg_printf("sigchld_handler: waitpid returned %d, status %d\n",
                   g_sigchld_pid, status);

        // if the child has exited (SIGINT or exit())
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            delete_job(jid);

            // print job status if received a signal
            if (WIFSIGNALED(status))
                sio_printf("Job [%d] (%d) terminated by signal %d\n", jid, pid,
                           WTERMSIG(status));
        } else if (WIFSTOPPED(status)) { // if has stopped (SIGTSTP)
            job_set_state(jid, ST);

            // print job status
            sio_printf("Job [%d] (%d) stopped by signal %d\n", jid, pid,
                       WSTOPSIG(status));
        }
    }

    sig_guard_end();
    errno = olderrno;
}

/**
 * Common routine used for handling SIGINT and SIGSTOP
 *
 * Kill all child processes.
 *
 * @param sig Signal number
 */
static void sig_int_tstp_handler(int sig) {
    dbg_assert(sig == SIGINT || sig == SIGTSTP);
    int olderrno = errno;

    sig_guard_start();
    jid_t jid = fg_job();
    dbg_printf("sig_int_tstp_handler: jid=%d\n", jid);
    if (jid) {
        pid_t pid = job_get_pid(jid);
        dbg_printf("sig_int_tstp_handler: pid=%d\n", pid);

        // kill every process with gid same as the fg job's pid
        if (kill(-pid, sig)) {
            perror("sig_int_tstp_handler: kill failed");
        }
    }
    sig_guard_end();

    errno = olderrno;
}

/**
 * Handles SIGINT.
 *
 * @param sig Signal number
 *
 * @see sig_int_tstp_handler
 */
void sigint_handler(int sig) {
    sig_int_tstp_handler(sig);
}

/**
 * Handles SIGTSTP.
 *
 * @param sig Signal number
 *
 * @see sig_int_tstp_handler
 */
void sigtstp_handler(int sig) {
    sig_int_tstp_handler(sig);
}

/**
 * @brief Attempt to clean up global resources when the program exits.
 *
 * In particular, the job list must be freed at this time, since it may
 * contain leftover buffers from existing or even deleted jobs.
 */
void cleanup(void) {
    // Signals handlers need to be removed before destroying the joblist
    Signal(SIGINT, SIG_DFL);  // Handles Ctrl-C
    Signal(SIGTSTP, SIG_DFL); // Handles Ctrl-Z
    Signal(SIGCHLD, SIG_DFL); // Handles terminated or stopped child

    destroy_job_list();
}

/*
 * Individual eval functions
 */

/**
 * Evaluate quit command
 */
static void eval_quit() {
    // let init process clean up the mess
    _exit(0);
}

/**
 * Evaluate jobs command
 */
static void eval_jobs() {
    sig_guard_start();
    list_jobs(STDOUT_FILENO);
    sig_guard_end();
}

/**
 * Parse jid and pid from commandline argument, in the format of pid or %jid
 *
 * @param cmd Command shown in the error message, such as fg or bg
 * @param arg Argument of the command
 *
 * @return true if success
 *
 * @note NOT Async-signal-safe
 */
static bool parse_jid_and_pid(const char *cmd, const char *arg, jid_t *jid,
                              pid_t *pid) {
    size_t n = strlen(arg);
    if (!n) {
        sio_eprintf("%s: argument must be a PID or %%jobid\n", cmd);
        return false;
    }

    // convert job id to pid
    if ('%' == arg[0]) {
        if (n < 2 || !(*jid = atoi(arg + 1))) {
            sio_eprintf("(%s): invalid jid\n", arg + 1);
            return false;
        }
        if (!job_exists(*jid)) {
            sio_eprintf("%s: No such job\n", arg);
            return false;
        }
        *pid = job_get_pid(*jid);
    } else { // check if pid is valid
        *pid = atoi(arg);
        if (!*pid) {
            sio_eprintf("%s: argument must be a PID or %%jobid\n", cmd);
            return false;
        }
        if (!(*jid = job_from_pid(*pid))) {
            sio_eprintf("(%s): No such process\n", arg);
            return false;
        }
    }

    return true;
}

/**
 * Evaluate bg command
 */
static void eval_bg(const char *arg) {
    sig_guard_start();

    jid_t jid = 0;
    pid_t pid = 0;
    if (parse_jid_and_pid("bg", arg, &jid, &pid)) {
        if (kill(-pid, SIGCONT)) {
            perror("eval_bg: kill failed");
            exit(1);
        }

        job_set_state(jid, BG);
        sio_printf("[%d] (%d) %s\n", jid, pid, job_get_cmdline(jid));
    }

    sig_guard_end();
}

/**
 * Evaluate fg command
 */
static void eval_fg(const char *arg) {
    sig_guard_start();

    jid_t jid = 0;
    pid_t pid = 0;
    if (parse_jid_and_pid("fg", arg, &jid, &pid)) {
        if (kill(-pid, SIGCONT)) {
            perror("eval_bg: kill failed");
            exit(1);
        }

        job_set_state(jid, FG);
        wait_fg_job();
    }

    sig_guard_end();
}

/**
 * Evaluate builtin commands
 * @param token Command tokens
 */
static void eval_builtin(struct cmdline_tokens token) {
    if (0 == strncmp(token.argv[0], "quit", 5)) {
        eval_quit();
    }

    if (0 == strncmp(token.argv[0], "jobs", 5)) {
        eval_jobs();
    }

    if (0 == strncmp(token.argv[0], "bg", 3)) {
        if (token.argc < 2) {
            sio_eprintf("bg command requires PID or %%jobid argument\n");
        } else {
            eval_bg(token.argv[1]);
        }
    }

    if (0 == strncmp(token.argv[0], "fg", 3)) {
        if (token.argc < 2) {
            sio_eprintf("fg command requires PID or %%jobid argument\n");
        } else {
            eval_fg(token.argv[1]);
        }
    }
}

/**
 * Prepare commonly used signal masks
 */
void init_sig_masks() {
    sigemptyset(&g_mask_empty);

    sigfillset(&g_mask_all);

    sigemptyset(&g_mask_chld_int_stp);
    sigaddset(&g_mask_chld_int_stp, SIGCHLD);
    sigaddset(&g_mask_chld_int_stp, SIGINT);
    sigaddset(&g_mask_chld_int_stp, SIGTSTP);
}

/**
 * Redirect IO to file or STDIN/STDOUT
 * @param infile Filename, use STDIN if NULL
 * @param outfile Filename, use STDOUT if NULL
 * @return true if success
 */
static bool redirect_io(const char *infile, const char *outfile) {
    if (infile) {
        int fd = open_file(infile, "r");
        if (-1 == fd) {
            return false;
        }

        if (dup2(fd, STDIN_FILENO) == -1) {
            perror("redirect_io");
            return false;
        }

        if (close(fd) == -1) {
            perror(infile);
            return false;
        }
    } else {
        if (dup2(g_stdin_fileno, STDIN_FILENO) == -1) {
            perror("redirect_io");
            return false;
        }
    }

    if (outfile) {
        int fd = open_file(outfile, "w");
        if (-1 == fd) {
            return false;
        }

        if (dup2(fd, STDOUT_FILENO) == -1) {
            perror("redirect_io: dup2() failed");
            return false;
        }

        if (close(fd) == -1) {
            perror(outfile);
            return false;
        }
    } else {
        if (dup2(g_stdout_fileno, STDOUT_FILENO) == -1) {
            perror("redirect_io");
            return false;
        }
    }

    return true;
}

/**
 * Open a file and get its file descriptor
 * @param filename Filename
 * @param mode File mode
 * @see fopen
 * @return file descriptor if success, otherwise -1
 */
int open_file(const char *filename, const char *mode) {
    FILE *file = fopen(filename, mode);
    if (!file) {
        perror(filename);
        return -1;
    }

    int fd = fileno(file);
    if (fd == -1) {
        perror(filename);
        return -1;
    }

    return fd;
}
