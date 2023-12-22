/* SPDX-License-Identifier: ISC */

#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#include <libite/lite.h>
#include <uev/uev.h>

#include "log.h"

#define HWMON_PATH "/sys/class/hwmon/"

static void poll_temp(uev_t *w, void *arg, int events)
{
	char *hwmon = (char *)arg;
	char buf[10];
	FILE *fp;

	fp = fopenf("r", HWMON_PATH "%s/temp1_input", hwmon);
	if (!fp)
		return;

	if (fgets(buf, sizeof(buf), fp)) {
		double temp;
		int tmp;

		chomp(buf);
		tmp  = atoi(buf);
		temp = (double)tmp / 1000;

		LOG("Current temperature %.1f°C", temp);
	}

	fclose(fp);
	
}

int main(int argc, char *argv[])
{
	int do_background = 1;
	int do_syslog  = 1;
	uev_ctx_t ctx;
	uev_t w;
	int c;

	while ((c = getopt(argc, argv, "hl:ns")) != EOF) {
		switch (c) {
		case 'h':
			printf("usage: %s [-hns] [-l LOG_LEVEL]\n", argv[0]);
			return 0;

		case 'l':
			if (-1 == log_level(optarg)) {
				ERR(errno, "Invalid log level");
				return 1;
			}
			break;

		case 'n':
			do_background = 0;
			do_syslog--;
			break;

		case 's':
			do_syslog++;
			break;

		default:
			return 1;
		}
	}

	log_init(do_syslog);

	if (do_background && -1 == daemon(0, 0)) {
		ERR(errno, "Failed daemonizing");
		return 1;
	}

	if (uev_init(&ctx)) {
		ERR(errno, "Failed creating loop context.");
		return 1;
	}

	uev_timer_init(&ctx, &w, poll_temp, "hwmon1", 100, 2000);

	return uev_run(&ctx, 0);
}
