sbin_PROGRAMS      = tempd

tempd_SOURCES      = main.c log.c log.h
tempd_CPPFLAGS     = -D_GNU_SOURCE -D_BSD_SOURCE -D_DEFAULT_SOURCE
tempd_CFLAGS       = -W -Wall -Wextra -Wno-unused-parameter
tempd_CFLAGS      += $(uev_CFLAGS) $(lite_CFLAGS)
tempd_LDADD        = $(uev_LIBS)   $(lite_LIBS)
