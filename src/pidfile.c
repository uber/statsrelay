#include "pidfile.h"
#include "config.h"

int read_pid(char *pidfile) {
	FILE *f;
	int pid, ret;

	if (!(f = fopen(pidfile,"r"))) {
		return 0;
	}

	if ((ret = fscanf(f, "%d", &pid)) != EOF) {
		fclose(f);
		return pid;
	}
	return 0;
}

int remove_pid(char *pidfile) {
	return unlink(pidfile);
}

int write_pid(char *pidfile, pid_t old_pid) {
	FILE *f;
	int fd, pid;

	if ((fd = open(pidfile, O_RDWR|O_CREAT, 0644)) == -1) {
		stats_error_log("pidfile: can't open or create %s.\n", pidfile);
		return 0;
	}

	if ((f = fdopen(fd, "r+")) == NULL) {
		stats_error_log("pidfile: fdopen failed\n");
		close(fd);
		return 0;
	}

#if HAVE_FLOCK
	if (flock(fd, LOCK_EX|LOCK_NB) == -1) {
		fscanf(f, "%d", &pid);
		fclose(f);
		stats_error_log("pidfile: can't lock, lock is held by pid %d.\n", pid);
		return 0;
	}
#endif

	pid = old_pid;

	if (!fprintf(f, "%d\n", pid)) {
		stats_error_log("pidfile: write failed %s", strerror(errno));
		close(fd);
		fclose(f);
		return 0;
	}
	fflush(f);

#if HAVE_FLOCK
	if (flock(fd, LOCK_UN) == -1) {
		stats_error_log("pidfile: Can't unlock pidfile %s, %s", pidfile, strerror(errno));
		close(fd);
		fclose(f);
		return 0;
	}
#endif
	close(fd);
	fclose(f);

	return pid;
}
