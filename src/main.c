#include "config.h"
#include "protocol.h"
#include "tcpserver.h"
#include "server.h"
#include "pidfile.h"

#include <ctype.h>
#include <getopt.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <stdlib.h>


struct server_collection servers;

static struct option long_options[] = {
    {"config",		required_argument,	NULL, 'c'},
    {"check-config",	required_argument,	NULL, 't'},
    {"verbose",		no_argument,		NULL, 'v'},
    {"pid",			required_argument,	NULL, 'p'},
    {"version",		no_argument,		NULL, 'V'},
    {"log-level",		required_argument,	NULL, 'l'},
    {"help",		no_argument,		NULL, 'h'},
    {"no-syslog",           no_argument,            NULL, 'S'},
};

static char **argv_ptr = NULL;
static char **envp_ptr = NULL;
static int num_args = 0;
static char *pid_file = NULL;
static int reexec_pid = 0;

const useconds_t QUIET_WAIT = 5000000; /* 5 seconds */

static void graceful_shutdown(struct ev_loop *loop, ev_signal *w, int revents) {
    /**
     * nuke old pidfile
     */
    char *buffer = (char *)malloc(sizeof(char) * 100);
    memset(buffer, '\0', 100);

    if (pid_file != NULL) {
        strcpy(buffer, pid_file);
        strcat(buffer, ".oldbin");
        remove_pid(buffer);
        stats_log("main: removing the oldbin file");
    }

    free(buffer);
    stats_log("main: received signal, shutting down.");

    ev_break(loop, EVBREAK_ALL);
}

static void quick_shutdown(struct ev_loop *loop, ev_signal *w, int revents) {
    stats_log("main: received signal, immediate shut down.");
    stop_accepting_connections(&servers);

    ev_break(loop, EVBREAK_ALL);
}


static void hot_restart(struct ev_loop *loop, ev_signal *w, int revents) {
    pid_t pid, old_pid;

    stats_log("main: received SIGUSR2, hot restarting.");

    /**
     * save the pid of the old master
     */
    old_pid = read_pid(pid_file);

    /**
     * handle re-exec
     */
    pid = reexec_pid = fork();

    if (pid < 0) {
        stats_error_log("main: failed to fork() on SIGUSR2!");
        stats_log("main: shutting down master.");
        stop_accepting_connections(&servers);

        ev_break(loop, EVBREAK_ALL);

        if (pid_file != NULL) {
            stats_log("removing pidfile: %s", pid_file);
            remove_pid(pid_file);
        }
    }

    if (pid) {
        char *buffer = (char *)malloc(sizeof(char) * 100);
        memset(buffer, '\0', 100);

        stats_debug_log("In parent process pid: %d, ppid:%d", getpid(), getppid());
        stats_debug_log("forked new child process with pid:%d", pid);

        /**
         * commence pseudo graceful shutdown of "Old Master"
         *
         * prevent parent from accepting new connections
         */
        stop_accepting_connections(&servers);

        /**
         * Inform the connected tcp clients
         * and wait to drain the read buffers
         */
        shutdown_client_sockets(&servers);

        /**
         *  Sleep for 5 seconds to allow
         *  sesssion_t buffer to be flushed fully
         */
        usleep(QUIET_WAIT);

        if (old_pid != 0) {
            strcpy(buffer, pid_file);
            strcat(buffer, ".oldbin");

            stats_log("main: backing up in old pid file %s", buffer);

            write_pid(buffer, old_pid);
        }
        free(buffer);

        return;
    } else if (pid == 0) {
        /**
         *  execv a copy of new master
         */
        stats_log("main: reexec %s.", argv_ptr[0]);
        execv(argv_ptr[0],  argv_ptr);

        /**
         * execv failed
         */
        stats_error_log("main: execv failed %s", strerror(errno));
        exit(1);
    }
}

static char* to_lower(const char *input) {
    char *output = strdup(input);
    for (int i  = 0; output[i] != '\0'; i++) {
        output[i] = tolower(output[i]);
    }
    return output;
}

static struct config *load_config(const char *filename) {
    FILE *file_handle = fopen(filename, "r");
    if (file_handle == NULL) {
        stats_error_log("main: failed to open file %s", servers.config_file);
        return NULL;
    }

    struct config *cfg;

    cfg = parse_json_config(file_handle);

    fclose(file_handle);
    return cfg;
}


static void print_help(const char *argv0) {
    printf("Usage: %s [options]\n"
            "  -h, --help                   Display this message\n"
            "  -S, --no-syslog              Do not write to syslog (use with -v to log to stderr)\n"
            "  -v, --verbose                Write log messages to stderr in addition to syslog\n"
            "                               syslog\n"
            "  -l, --log-level              Set the logging level to DEBUG, INFO, WARN, or ERROR\n"
            "                               (default: INFO)\n"
            "  -p  --pid                    Path to the pid file\n"
            "  -c, --config=filename        Use the given hashring config file\n"
            "                               (default: %s)\n"
            "  -t, --check-config=filename  Check the config syntax\n"
            "                               (default: %s)\n"
            "  --version                    Print the version\n",
            argv0,
            default_config,
            default_config);
}

int main(int argc, char **argv, char **envp) {
    ev_signal sigint_watcher, sigterm_watcher, sigusr2_watcher;
    char *lower;
    char c = 0;
    bool just_check_config = false;
    struct config *cfg = NULL;
    servers.initialized = false;

    argv_ptr = argv;
    num_args = argc;
    envp_ptr = envp;

    stats_set_log_level(STATSRELAY_LOG_INFO);  // set default value

#ifdef HAVE_MALLOC_H
    /**
     * Send detailed error message, stack trace, and memory
     * mappings to stderr
     */
    setenv("LIBC_FATAL_STDERR_", "1", 1);

    /**
     *  detects heap corruption, diagnostic
     *  will be logged.
     */
    if (mallopt(M_CHECK_ACTION, 3) != 1) {
        stats_error_log("mallopt() failed: MALLOC_CHECK_ not set");
    }
#endif

    while (c != -1) {
        c = getopt_long(argc, argv, "t:c:l:p:VvhS", long_options, NULL);
        switch (c) {
            case -1:
                break;
            case 0:
            case 'h':
                print_help(argv[0]);
                return 1;
            case 'S':
                stats_log_syslog(false);
                break;
            case 'v':
                stats_log_verbose(true);
                break;
            case 'V':
                puts(PACKAGE_STRING);
                return 0;
            case 'p':
                pid_file = optarg;
                break;
            case 'l':
                lower = to_lower(optarg);
                if (lower == NULL) {
                    fprintf(stderr, "main: unable to allocate memory\n");
                    goto err;
                }
                if (strcmp(lower, "debug") == 0) {
                    stats_set_log_level(STATSRELAY_LOG_DEBUG);
                    stats_log_verbose(true);
                } else if (strcmp(lower, "warn") == 0) {
                    stats_set_log_level(STATSRELAY_LOG_WARN);
                } else if (strcmp(lower, "error") == 0) {
                    stats_set_log_level(STATSRELAY_LOG_ERROR);
                }
                free(lower);
                break;
            case 'c':
                init_server_collection(&servers, optarg);
                break;
            case 't':
                init_server_collection(&servers, optarg);
                just_check_config = true;
                break;
            default:
                fprintf(stderr, "%s: Unknown argument %c\n", argv[0], c);
                goto err;
        }
    }
    stats_log(PACKAGE_STRING);

    if (!servers.initialized) {
        init_server_collection(&servers, default_config);
    }

    cfg = load_config(servers.config_file);
    if (cfg == NULL) {
        stats_error_log("failed to parse config");
        goto err;
    }
    if (just_check_config) {
        goto success;
    }
    bool worked = connect_server_collection(&servers, cfg);
    if (!worked) {
        goto err;
    }

    if (pid_file != NULL) {
        write_pid(pid_file, getpid());
    }

    struct ev_loop *loop = ev_default_loop(0);
    ev_signal_init(&sigint_watcher, quick_shutdown, SIGINT);
    ev_signal_start(loop, &sigint_watcher);

    ev_signal_init(&sigterm_watcher, graceful_shutdown, SIGTERM);
    ev_signal_start(loop, &sigterm_watcher);

    ev_signal_init(&sigusr2_watcher, hot_restart, SIGUSR2);
    ev_signal_start(loop, &sigusr2_watcher);

    stats_log("main(%d): Starting event loop.", getpid());
    ev_run(loop, 0);
    stats_log("main(%d): Loop terminated. Goodbye.", getpid());

success:
    stop_accepting_connections(&servers);
    shutdown_client_sockets(&servers);
    destroy_server_collection(&servers);
    destroy_json_config(cfg);
    stats_log_end();
    return 0;

err:
    stop_accepting_connections(&servers);
    shutdown_client_sockets(&servers);
    destroy_server_collection(&servers);
    destroy_json_config(cfg);
    stats_log_end();
    return 1;
}
