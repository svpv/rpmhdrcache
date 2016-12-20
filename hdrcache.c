#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <rpm/rpmlib.h>
#include "rpmcache.h"
#include "hdrcache.h"

static __thread
struct rpmcache *cache;

static
void finalize()
{
    rpmcache_close(cache);
}

static inline
const char *opt_(const char *name)
{
    const char *str = getenv(name);
    return (str && *str) ? str : NULL;
}

#define opt(name) opt_("RPMHDRCACHE_" name)

static
int initialize()
{
    static __thread
    int initialized;
    if (initialized)
	return initialized;
    if (opt("DISABLE")) {
	initialized = -1;
	return initialized;
    }
    const char *dir = opt("DIR");
    if (dir == NULL)
	dir = "rpmhdrcache";
    cache = rpmcache_open(dir);
    if (cache == NULL) {
	initialized = -1;
	return initialized;
    }
    initialized = 1;
    atexit(finalize);
    return initialized;
}

#include "error.h"

struct cache_ent {
    unsigned off;
    char blob[1];
};

Header hdrcache_get(const char *path, const struct stat *st, unsigned *off)
{
    if (initialize() < 0)
	return NULL;
    struct cache_ent *data;
    int datasize;
    if (!rpmcache_get(cache, path, st->st_size, st->st_mtime, (void **) &data, &datasize))
	return NULL;
    Header h = headerCopyLoad(data->blob);
    if (h == NULL) {
	const char *bn = strrchr(path, '/');
	bn = bn ? bn + 1 : path;
	ERROR("%s: headerLoad failed", bn);
	return NULL;
    }
    if (off)
	*off = data->off;
    free(data);
    return h;
}

void hdrcache_put(const char *path, const struct stat *st, Header h, unsigned off)
{
    if (initialize() < 0)
	return;
    int blobsize = headerSizeof(h, HEADER_MAGIC_NO);
    void *blob = headerUnload(h);
    if (blob == NULL) {
	const char *bn = strrchr(path, '/');
	bn = bn ? bn + 1 : path;
	ERROR("%s: headerLoad failed", bn);
	return;
    }
    int datasize = sizeof(unsigned) + blobsize;
    struct cache_ent *data = malloc(datasize);
    if (data == NULL) {
	ERROR("malloc: %m");
	return;
    }
    data->off = off;
    memcpy(data->blob, blob, blobsize);
    free(blob);
    rpmcache_put(cache, path, st->st_size, st->st_mtime, data, datasize);
    free(data);
}

// ex: set ts=8 sts=4 sw=4 noet:
