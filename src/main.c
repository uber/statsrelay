#include "config.h"
#include "protocol.h"
#include "tcpserver.h"
#include "udpserver.h"
#include "server.h"
#include "stats.h"
#include "log.h"
#include "validate.h"
#include "json_config.h"

#include <assert.h>
#include <ctype.h>
#include <ev.h>
#include <getopt.h>
#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct server_collection servers;

static struct option long_options[] = {
	{"config",		required_argument,	NULL, 'c'},
	{"check-config",	required_argument,	NULL, 't'},
	{"verbose",		no_argument,		NULL, 'v'},
	{"version",		no_argument,		NULL, 'V'},
	{"log-level",		required_argument,	NULL, 'l'},
	{"help",		no_argument,		NULL, 'h'},
	{"no-syslog",           no_argument,            NULL, 'S'},
};

static char **argv_ptr = NULL;
static char **envp_ptr = NULL;
static int num_args = 0;

static void graceful_shutdown(struct ev_loop *loop, ev_signal *w, int revents) {
	stats_log("Received signal, shutting down.");
	destroy_server_collection(&servers);
	ev_break(loop, EVBREAK_ALL);
}

static void reload_config(struct ev_loop *loop, ev_signal *w, int revents) {
	stats_log("Received SIGHUP, reloading.");
	if (server != NULL) {
		stats_server_reload(server);
	}
}

static void hot_restart(struct ev_loop *loop, ev_signal *w, int revents) {
	pid_t pid;
	stats_log("Received SIGUSR2, hot restarting...");

	/**
	 * handle re-exec
	 */
	pid = fork();

	if (pid < 0) {
		stats_error_log("Failed to fork() on SIGUSR2!");
		stats_log("Shutting down master.");
		destroy_server_collection(&servers);
		ev_break(loop, EVBREAK_ALL);
	}

	if (pid) {
		stats_debug_log("In parent process pid: %d, ppid:%d", getpid(), getppid());
		stats_debug_log("forked new child process with pid:%d", pid);

		/**
		 * commence pseudo graceful shutdown of "Old Master"
		 * 
		 * 1. prevent parent from accepting new connections
		 */
		stop_accepting_connections(&servers);

		/**
		 * 2. wait for all the open sockets to be closed (via shutdown, read, close)
		 * 3. Close listener->sd
		 * 4. inform rainbow-saddle?
		 * 5. graceful terminate self (dont know how)
		 */
		return 0;
	}

	/**
	 *  execv a copy of new master
	 */
	stats_log("reexec %s...", argv_ptr[0]);
	execv(argv_ptr[0],  argv_ptr);

	/**
	 * execv failed
	 */
	stats_error_log("execv failed");
	exit(1);
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
		stats_error_log("failed to open file %s", servers.config_file);
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
	ev_signal sigint_watcher, sigterm_watcher, sighup_watcher, sigusr2_watcher, sigwinch_watcher;
	char *lower;
	char c = 0;
	bool just_check_config = false;
	struct config *cfg = NULL;
	servers.initialized = false;

	argv_ptr = argv;
	num_args = argc;
	envp_ptr = envp;

	stats_set_log_level(STATSRELAY_LOG_INFO);  // set default value
	while (c != -1) {
		c = getopt_long(argc, argv, "t:c:l:vhS", long_options, NULL);
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

	struct ev_loop *loop = ev_default_loop(0);
	ev_signal_init(&sigint_watcher, graceful_shutdown, SIGINT);
	ev_signal_start(loop, &sigint_watcher);

	ev_signal_init(&sigterm_watcher, graceful_shutdown, SIGTERM);
	ev_signal_start(loop, &sigterm_watcher);

	ev_signal_init(&sighup_watcher, reload_config, SIGHUP);
	ev_signal_start(loop, &sighup_watcher);

	ev_signal_init(&sigusr2_watcher, hot_restart, SIGUSR2);
	ev_signal_start(loop, &sigusr2_watcher);

	ev_signal_init(&sigwinch_watcher, graceful_shutdown, SIGWINCH);
	ev_signal_start(loop, &sigwinch_watcher);

	stats_log("main: Starting event loop");
	ev_run(loop, 0);

success:
	destroy_server_collection(&servers);
	destroy_json_config(cfg);
	stats_log_end();
	return 0;

err:
	destroy_server_collection(&servers);
	destroy_json_config(cfg);
	stats_log_end();
	return 1;
}
