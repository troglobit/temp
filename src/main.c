/* SPDX-License-Identifier: ISC */

#include <errno.h>
#include <dirent.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <libite/lite.h>
#include <libite/queue.h>
#include <uev/uev.h>

#include "log.h"

#define POLL_INTERVAL 2000

#define HWMON_PATH    "/sys/class/hwmon/"
#define HWMON_NAME    "temp%d_label"
#define HWMON_TEMP    "temp%d_input"
#define HWMON_TRIP    "temp%d_crit"
#define HWMON_TALT    "temp%d_max"
#define THERMAL_PATH  "/sys/class/thermal/"
#define THERMAL_TRIP  "trip_point_0_temp"

struct temp {
	TAILQ_ENTRY(temp) link; /* BSD sys/queue.h linked list node. */

	int   id;
	char  name[32];
	char *temp;
	char *crit;

	float tcrit;
	float tdata[10];
	int   tdpos;

	uev_t watcher;
};

static TAILQ_HEAD(shead, temp) sensors = TAILQ_HEAD_INITIALIZER(sensors);


static char *paste(char *path, size_t len, char *file, size_t offset)
{
	if (offset >= len)
		return NULL;

	if (offset > 0)
		path[offset] = 0;
	strlcat(path, file, len);

	return path;
}

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

static void write_file(uev_t *w, void *arg, int events)
{
	char *fn = (char *)arg;
	struct temp *s;
	FILE *fp;

	fp = fopen(fn, "w");
	if (!fp) {
		ERR(errno, "Failed writing to %s", fn);
		return;
	}

	fprintf(fp, "[\n");
	TAILQ_FOREACH(s, &sensors, link) {
		fprintf(fp, "  {\n");
		fprintf(fp, "    \"name\": \"%s\",\n", s->name);
		fprintf(fp, "    \"file\": \"%s\",\n", s->temp);
		if (s->crit)
			fprintf(fp, "    \"critical\": \"%.1f\",\n", s->tcrit);
		fprintf(fp, "    \"temperature\": [ ");
		for (size_t i = 0; i < NELEMS(s->tdata); i++)
			fprintf(fp, "%s\"%.1f\"", i != 0 ? ", " : "", s->tdata[i]);
		fprintf(fp, " ],\n");
		fprintf(fp, "    \"interval\": %d\n", POLL_INTERVAL);
		fprintf(fp, "  }%s\n", TAILQ_NEXT(s, link) != TAILQ_END(&sensors) ? "," : "");
	}
	fprintf(fp, "]\n");

	fclose(fp);
}

static float read_temp(const char *path)
{
	float temp = 0.0;
	char buf[10];

	/* could be sensor with missing crit/max/trip, ignore */
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

static int sanity_check(const char *path)
{
	float temp = read_temp(path);

	if (temp == 0.0 ||  temp < -150.0 || temp > 150.0)
		return 1;

	return 0;
}

static char *sensor_hwmon(struct temp *sensor, char *temp, char *path, size_t len)
{
	size_t offset = strlen(path);
	char file[32];

	if (sscanf(&temp[offset], "temp%d_input", &sensor->id) != 1) {
		INFO("Failed reading ID from %s", temp);
		goto fail;
	}

	DBG("Got ID %d", sensor->id);
	if (sanity_check(temp)) {
		INFO("Improbable value detected, skipping %s", temp);
		goto fail;
	}

	snprintf(file, sizeof(file), HWMON_NAME, sensor->id);
	if (!read_file(paste(path, len, file, offset), sensor->name, sizeof(sensor->name)))
		read_file(paste(path, len, "name", offset), sensor->name, sizeof(sensor->name));

	snprintf(file, sizeof(file), HWMON_TALT, sensor->id);
	if (fexist(paste(path, len, file, offset)))
		sensor->crit = path;

	snprintf(file, sizeof(file), HWMON_TRIP, sensor->id);
	if (fexist(paste(path, len, file, offset)))
		sensor->crit = path;
fail:
	if (!sensor->crit || sanity_check(sensor->crit)) {
		sensor->crit = NULL;
		free(path);
	}

	return sensor->name[0] ? sensor->name : NULL;
}

static char *sensor_thermal(struct temp *sensor, char *temp, char *path, size_t len)
{
	size_t offset = strlen(path);

	if (sscanf(temp, THERMAL_PATH "thermal_zone%d/temp", &sensor->id) != 1) {
		INFO("Failed reading ID from %s", temp);
		return NULL;
	}

	DBG("Got ID %d", sensor->id);
	read_file(paste(path, len, "type", offset), sensor->name, sizeof(sensor->name));

	if (fexist(paste(path, len, THERMAL_TRIP, offset)))
		sensor->crit = path;

	if (!sensor->crit || sanity_check(sensor->crit)) {
		sensor->crit = NULL;
		free(path);
	}

	return sensor->name[0] ? sensor->name : NULL;
}

static char *find_sensor(struct temp *sensor, char *temp)
{
	size_t len = strlen(temp) + 32;
	char *path, *ptr;

	path = malloc(len);
	if (!path) {
		ERR(errno, "Critical, cannot get memory for %s", temp);
		return NULL;
	}

	strlcpy(path, temp, len);
	ptr = rindex(path, '/');
	if (!ptr)
		goto fail;
	*(++ptr) = 0;

	DBG("Base path %s len %zd", path, len);
	if (!strncmp(path, HWMON_PATH, strlen(HWMON_PATH)))
		return sensor_hwmon(sensor, temp, path, len);
	else if (!strncmp(path, THERMAL_PATH, strlen(THERMAL_PATH)))
		return sensor_thermal(sensor, temp, path, len);

fail:
	ERR(0, "This does not look like a temp sensor %s", temp);
	free(path);

	return NULL;
}

static void add_sensor(char *path, int probe)
{
	struct temp *sensor;

	if (!fexist(path)) {
		if (!probe)
			ERR(errno, "Missing sensor %s, skipping", path);
		return;
	}

	sensor = calloc(1, sizeof(*sensor));
	if (!sensor)
		goto fail;

	DBG("Checking sensor %s ...", path);
	sensor->temp = strdup(path);
	if (!sensor->temp) {
	fail:
		if (!probe)
			ERR(errno, "Failed setting up sensor %s", path);
		free(sensor);
		return;
	}

	if (!find_sensor(sensor, path)) {
		if (!probe)
			ERR(0, "Cannot find sensor %s, skipping.", sensor->temp);
		free(sensor->temp);
		free(sensor);
		return;
	}

	TAILQ_INSERT_TAIL(&sensors, sensor, link);
}

static int find_hwmon(void)
{
	struct dirent **list, *d;
	int i = 0, num;

	num = scandir(HWMON_PATH, &list, NULL, alphasort);
	for (d = list[i]; i < num; d = list[++i]) {
		char path[sizeof(HWMON_PATH) + strlen(d->d_name) + 14];

		if (d->d_type != DT_LNK)
			continue;

		DBG("Probed sensor: %s", d->d_name);
		for (int j = 1; j < 10; j++) {
			snprintf(path, sizeof(path), "%s%s/temp%d_input", HWMON_PATH, d->d_name, j);
			add_sensor(path, 1);
		}

		free(d);
	}
	free(list);

	return TAILQ_EMPTY(&sensors);
}

static float calc_mean(struct temp *sensor)
{
	size_t i, valid = 0, num = NELEMS(sensor->tdata);
	float  mean = 0.0;

	for (i = 0; i < num; i++) {
		float tdata = sensor->tdata[i];

		if (tdata != 0.0)
			valid++;
		mean += tdata;
	}

	return mean / valid;
}

static void poll_temp(uev_t *w, void *arg, int events)
{
	struct temp *sensor = (struct temp *)arg;
	char crit[32] = { 0 };
	float temp;

	temp = read_temp(sensor->temp);
	sensor->tdata[sensor->tdpos++] = temp;
	if (sensor->tdpos == NELEMS(sensor->tdata))
		sensor->tdpos = 0;

	if (sensor->crit)
		snprintf(crit, sizeof(crit), ", critical %.1f°C", sensor->tcrit);

	LOG("%15s: current %.1f°C, mean %.1f°C%s", sensor->name,
	    temp, calc_mean(sensor), crit);
}

static void term(uev_t *w, void *arg, int events)
{
	struct temp *sensor, *tmp;

	INFO("Received signal %d, exiting ...", w->signo);

	TAILQ_FOREACH_SAFE(sensor, &sensors, link, tmp) {
		uev_timer_stop(&sensor->watcher);

		TAILQ_REMOVE(&sensors, sensor, link);
		free(sensor->temp);
		if (sensor->crit)
			free(sensor->crit);
		free(sensor);
	}

	uev_exit(w->ctx);
}

int main(int argc, char *argv[])
{
	int do_background = 1;
	int do_syslog  = 1;
	char *file = NULL;
	struct temp *s;
	uev_ctx_t ctx;
	uev_t sigterm;
	uev_t sigint;
	uev_t filer;
	int c;

	while ((c = getopt(argc, argv, "hf:l:nst:")) != EOF) {
		switch (c) {
		case 'h':
			printf("usage: %s [-hns] [-f FILE] [-l LOG_LEVEL] [-t PATH]\n", argv[0]);
			return 0;

		case 'f':
			file = optarg;
			break;

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
			add_sensor(optarg, 0);
			break;

		default:
			return 1;
		}
	}

	if (TAILQ_EMPTY(&sensors)) {
		if (find_hwmon()) {
			ERR(0, "Need at least one temp sensor to start.");
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

	uev_signal_init(&ctx, &sigterm, term, NULL, SIGTERM);
	uev_signal_init(&ctx, &sigint,  term, NULL, SIGINT);

	TAILQ_FOREACH(s, &sensors, link) {
		s->tcrit = read_temp(s->crit);
		uev_timer_init(&ctx, &s->watcher, poll_temp, s, 100, POLL_INTERVAL);
	}

	if (file)
		uev_timer_init(&ctx, &filer, write_file, file, 100, POLL_INTERVAL);

	return uev_run(&ctx, 0);
}
