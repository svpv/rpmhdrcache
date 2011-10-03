#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "cache.h"

int main(int argc, const char *argv[])
{
    if (argc < 3) {
  usage:
      fprintf(stderr, "Usage: %s DAYS DIR...\n",
	      program_invocation_short_name);
      return 2;
    }
    int days = atoi(argv[1]);
    if (days < 1)
	goto usage;
    int rc = 0;
    int i;
    for (i = 2; i < argc; i++) {
	const char *dir = argv[i];
	struct cache *cache = cache_open(dir);
	if (!cache) {
	    // warning issued by the library
	    rc = 1;
	    continue;
	}
	cache_clean(cache, days);
	cache_close(cache);
    }
    return rc;
}

// ex: set ts=8 sts=4 sw=4 noet:
