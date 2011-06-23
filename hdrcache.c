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
#include "cache.h"
#include "hdrcache.h"

static __thread
struct cache *cache;

static
void finalize()
{
    cache_close(cache);
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
	dir = "/tmp/.rpmhdrcache";
    cache = cache_open(dir);
    if (cache == NULL) {
	initialized = -1;
	return initialized;
    }
    initialized = 1;
    atexit(finalize);
    return initialized;
}

static
int make_key(const char *path, const struct stat *st, char *key)
{
    const char *bn = strrchr(path, '/');
    bn = bn ? (bn + 1) : path;
    strcpy(key, bn);
    unsigned sm[2] = { st->st_size, st->st_mtime };
    int len = strlen(bn);
    memcpy(key + len + 1, sm, sizeof sm);
    return len + 1 + sizeof sm;
}

#define ERROR(fmt, args...) \
    fprintf(stderr, "%s: %s: " fmt "\n", \
	    program_invocation_short_name, __func__, ##args)

struct cache_ent {
    unsigned off;
    char blob[1];
};

Header hdrcache_get(const char *path, const struct stat *st, unsigned *off)
{
    if (initialize() < 0)
	return NULL;
    char key[4096];
    int keysize = make_key(path, st, key);
    struct cache_ent *data;
    int datasize;
    if (!cache_get(cache, key, keysize, (void **) &data, &datasize))
	return NULL;
    Header h = headerCopyLoad(data->blob);
    if (h == NULL) {
	ERROR("%s: headerLoad failed", key);
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
    char key[4096];
    int keysize = make_key(path, st, key);
    int blobsize = headerSizeof(h, HEADER_MAGIC_NO);
    void *blob = headerUnload(h);
    if (blob == NULL) {
	ERROR("%s: headerLoad failed", key);
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
    cache_put(cache, key, keysize, data, datasize);
}

// ex: set ts=8 sts=4 sw=4 noet:
