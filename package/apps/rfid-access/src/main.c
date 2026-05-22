#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#define DEVICE       "/dev/rc522"
#define ACCESS_LIST  "/etc/rfid/access.list"
#define LOG_FILE     "/var/log/rfid-access.log"
#define UID_LEN      16   /* 4-byte UID = 8 hex chars + NUL, 16 is safe */
#define DEBOUNCE_S   2    /* ignore same UID re-read within this many seconds */

static void log_access(const char *uid, int granted)
{
	FILE *log;
	struct timespec ts;
	long h, m, s;

	clock_gettime(CLOCK_BOOTTIME, &ts);
	h = ts.tv_sec / 3600;
	m = (ts.tv_sec % 3600) / 60;
	s = ts.tv_sec % 60;

	log = fopen(LOG_FILE, "a");
	if (!log)
		return;

	fprintf(log, "[%02ld:%02ld:%02ld] UID=%s %s\n", h, m, s, uid,
		granted ? "ACCEPTED" : "REJECTED");
	fclose(log);
}

static int check_access(const char *uid)
{
	FILE *list;
	char line[UID_LEN + 2];

	list = fopen(ACCESS_LIST, "r");
	if (!list)
		return 0;

	while (fgets(line, sizeof(line), list)) {
		line[strcspn(line, "\n")] = 0;
		if (strcmp(line, uid) == 0) {
			fclose(list);
			return 1;
		}
	}

	fclose(list);
	return 0;
}

int main(void)
{
	int fd;
	char uid[UID_LEN];
	char last_uid[UID_LEN] = { 0 };
	ssize_t n;
	int granted;
	struct timespec last_seen = { 0, 0 };
	struct timespec now;

	printf("RFID Access Control - starting\n");

	for (;;) {
		/*
		 * Open and close on every iteration: the driver uses the file
		 * offset to detect EOF, so the fd must be reopened each time
		 * to reset it.
		 */
		fd = open(DEVICE, O_RDONLY);
		if (fd < 0) {
			perror("open /dev/rc522");
			sleep(1);
			continue;
		}

		n = read(fd, uid, sizeof(uid) - 1);
		close(fd);

		if (n < 0) {
			/* ETIMEDOUT = no card; EIO = partial read (card moved), both are transient */
			if (errno != ETIMEDOUT && errno != EIO)
				perror("read /dev/rc522");
			continue;
		}

		uid[n] = 0;
		uid[strcspn(uid, "\n")] = 0;

		/*
		 * Debounce: skip if the same UID was already processed within
		 * DEBOUNCE_S seconds (card held on reader).
		 */
		clock_gettime(CLOCK_BOOTTIME, &now);
		if (strcmp(uid, last_uid) == 0 &&
		    (now.tv_sec - last_seen.tv_sec) < DEBOUNCE_S)
			continue;

		strncpy(last_uid, uid, sizeof(last_uid) - 1);
		last_uid[sizeof(last_uid) - 1] = 0;
		last_seen = now;

		granted = check_access(uid);

		printf("UID=%s %s\n", uid, granted ? "ACCEPTED" : "REJECTED");
		log_access(uid, granted);
	}

	return 0;
}