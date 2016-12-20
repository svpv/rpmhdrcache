#ifndef CACHE_ERROR_H
#define CACHE_ERROR_H

#include <stdio.h>
#include <errno.h>

#define ERROR(fmt, args...) \
    fprintf(stderr, "%s: %s: " fmt "\n", \
	    program_invocation_short_name, __func__, ##args)

#endif
