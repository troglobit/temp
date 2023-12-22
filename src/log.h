/* SPDX-License-Identifier: ISC */

#ifndef TEMP_LOG_H_
#define TEMP_LOG_H_

#include <syslog.h>

#define LOGIT(severity, code, fmt, args...)				\
	do {								\
		if (code)						\
			logit(severity, fmt ". Error %d: %s\n",		\
			      ##args, code, strerror(code));		\
		else							\
			logit(severity, fmt "\n", ##args);		\
	} while (0)
#define ERR(code, fmt, args...)  LOGIT(LOG_ERR, code, fmt, ##args)
#define WARN(code, fmt, args...) LOGIT(LOG_WARNING, code, fmt, ##args)
#define LOG(fmt, args...)        LOGIT(LOG_NOTICE, 0, fmt, ##args)
#define INFO(fmt, args...)       LOGIT(LOG_INFO, 0, fmt, ##args)
#define DBG(fmt, args...)        LOGIT(LOG_DEBUG, 0, fmt, ##args)

void log_init  (int use_syslog);
int  log_level (char *level);

void logit     (int severity, const char *fmt, ...);

#endif /* TEMP_LOG_H_ */
