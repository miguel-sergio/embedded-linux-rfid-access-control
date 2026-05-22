#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define DEVICE      "/dev/rc522"
#define ACCESS_LIST "/etc/rfid/access.list"
#define LOG_FILE    "/var/log/rfid-access.log"
#define UID_LEN     64

static void log_access(const char *uid, int granted)
{
	FILE *log;
	time_t now;
	char timestamp[32];

	time(&now);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S",
		 localtime(&now));

	log = fopen(LOG_FILE, "a");
	if (!log)
		return;

	fprintf(log, "%s UID=%s %s\n", timestamp, uid,
		granted ? "ACCEPTED" : "REJECTED");
	fclose(log);
}

static int check_access(const char *uid)
{
	FILE *list;
	char line[UID_LEN];

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
	ssize_t n;
	int granted;

	printf("RFID Access Control - starting\n");

	fd = open(DEVICE, O_RDONLY);
	if (fd < 0) {
		perror("open /dev/rc522");
		return 1;
	}

	n = read(fd, uid, sizeof(uid) - 1);
	if (n < 0) {
		perror("read");
		close(fd);
		return 1;
	}

	uid[n] = 0;
	uid[strcspn(uid, "\n")] = 0;

	close(fd);

	granted = check_access(uid);

	printf("UID=%s %s\n", uid, granted ? "ACCEPTED" : "REJECTED");
	log_access(uid, granted);

	return 0;
}