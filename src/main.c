/* SPDX-License-Identifier: ISC */

#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#include <libite/lite.h>
#include <libite/queue.h>
#include <uev/uev.h>

#include "log.h"

#define HWMON_PATH    "/sys/class/hwmon/"
#define HWMON_TRIP    "temp1_crit"
#define THERMAL_PATH  "/sys/class/thermal/"
#define THERMAL_TRIP  "trip_point_0_temp"

struct temp {
	TAILQ_ENTRY(temp) link; /* BSD sys/queue.h linked list node. */

	char  name[32];
	char *temp;
	char *crit;

	float tcrit;
	float data[10];
	int   pos;

	uev_t watcher;
};

static TAILQ_HEAD(shead, temp) sensors = TAILQ_HEAD_INITIALIZER(sensors);

static char *read_file(const char *fn, char *buf, size_t len)
{
	char *ptr = NULL;
	FILE *fp;

	fp = fopen(fn, "r");
	if (fp) {
		if ((ptr = fgets(buf, len, fp)))
			chomp(buf);
		fclose(fp);
	}

	return ptr;
}

static char *find_sensor(struct temp *sensor, char *path)
{
	size_t len = strlen(path) + strlen(THERMAL_TRIP) + 3;
	char *crit, *ptr;

	crit = malloc(len);
	if (!crit) {
		ERR(errno, "Critical, cannot get memory for %s", path);
		return NULL;
	}

	strlcpy(crit, path, len);
	ptr = rindex(crit, '/');
	if (!ptr)
		goto fail;
	*(++ptr) = 0;

	DBG("Base path %s len %zd", crit, len);
	if (!strncmp(crit, HWMON_PATH, strlen(HWMON_PATH))) {
		strlcat(crit, "name", len);
		read_file(crit, sensor->name, sizeof(sensor->name));

		*ptr = 0;
		strlcat(crit, HWMON_TRIP, len);
	} else if (!strncmp(crit, THERMAL_PATH, strlen(THERMAL_PATH))) {
		strlcat(crit, "type", len);
		read_file(crit, sensor->name, sizeof(sensor->name));

		*ptr = 0;
		strlcat(crit, THERMAL_TRIP, len);
	} else
		goto fail;

	DBG("Critical path %s", crit);
	return sensor->crit = crit;
fail:
	ERR(0, "This does not look like a temp sensor %s", path);
	free(crit);
	return NULL;
}

static void add_sensor(char *path)
{
	struct temp *sensor;

	if (!fexist(path)) {
		ERR(errno, "Missing sensor %s, skipping", path);
		return;
	}

	sensor = calloc(1, sizeof(*sensor));
	if (!sensor)
		goto fail;

	sensor->temp = strdup(path);
	if (!sensor->temp) {
	fail:
		ERR(errno, "Failed setting up sensor %s", path);
		free(sensor);
		return;
	}

	if (!find_sensor(sensor, path)) {
		ERR(ENOENT, "Cannot find sensor %s, skipping!", sensor->temp);
		free(sensor->temp);
		free(sensor);
		return;
	}

	TAILQ_INSERT_TAIL(&sensors, sensor, link);
}

static float calc_mean(struct temp *sensor)
{
	size_t i, valid = 0, num = NELEMS(sensor->data);
	float  mean = 0.0;

	for (i = 0; i < num; i++) {
		float data = sensor->data[i];

		if (data != 0.0)
			valid++;
		mean += data;
	}

	return mean / valid;
}

static float read_temp(const char *path)
{
	float temp = 0.0;
	char buf[10];

	if (!path)
		return temp;

	DBG("Reading sensor %s", path);
	if (read_file(path, buf, sizeof(buf))) {
		int tmp;

		DBG("Raw temp %s", buf);
		tmp  = atoi(buf);
		temp = (float)tmp / 1000;
		DBG("Got temp %.1f°C", temp);
	}

	return temp;
}

static void poll_temp(uev_t *w, void *arg, int events)
{
	struct temp *sensor = (struct temp *)arg;
	float temp;

	temp = read_temp(sensor->temp);
	sensor->data[sensor->pos++] = temp;
	if (sensor->pos == NELEMS(sensor->data))
		sensor->pos = 0;

	LOG("%15s: current %.1f°C, mean %.1f°C, critical %.1f°C", sensor->name,
	    temp, calc_mean(sensor), sensor->tcrit);
}

int main(int argc, char *argv[])
{
	int do_background = 1;
	int do_syslog  = 1;
	struct temp *s;
	uev_ctx_t ctx;
	int c;

	while ((c = getopt(argc, argv, "hl:nst:")) != EOF) {
		switch (c) {
		case 'h':
			printf("usage: %s [-hns] [-l LOG_LEVEL] [-t PATH]\n", argv[0]);
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

		case 't':
			add_sensor(optarg);
			break;

		default:
			return 1;
		}
	}

	if (TAILQ_EMPTY(&sensors)) {
		ERR(0, "Need at least one temp sensor to start.");
		return 1;
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

	TAILQ_FOREACH(s, &sensors, link) {
		s->tcrit = read_temp(s->crit);
		uev_timer_init(&ctx, &s->watcher, poll_temp, s, 100, 2000);
	}

	return uev_run(&ctx, 0);
}
