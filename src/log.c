
/* SPDX-License-Identifier: ISC */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <libite/lite.h>

#define SYSLOG_NAMES
#include "log.h"

static int print = 1;
static int level = LOG_NOTICE;

void log_init(int use_syslog)
{
	if (use_syslog) {
		print = 0;
		openlog(NULL, LOG_PID, LOG_DAEMON);
	}
}

int log_level(char *arg)
{
	for (int i = 0; prioritynames[i].c_name; i++) {
		if (string_match(prioritynames[i].c_name, arg))
			return level = prioritynames[i].c_val;
	}

	return level = atoi(arg);
}

void logit(int severity, const char *fmt, ...)
{
        va_list args;
	FILE *file;

	if (level == INTERNAL_NOPRI)
		return;

	if (severity > LOG_WARNING)
		file = stdout;
	else
		file = stderr;

        va_start(args, fmt);
	if (!print)
		vsyslog(severity, fmt, args);
	else if (severity <= level) {
		if (level == LOG_DEBUG)
			fprintf(file, "%d> ", getpid());
		vfprintf(file, fmt, args);
		fflush(file);
	}
        va_end(args);
}
