#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <db.h>
#include "error.h"

#define SET_UMASK(cache) \
    cache->omask = umask(cache->umask)
#define UNSET_UMASK(cache) \
    if (cache->omask != cache->umask) \
	umask(cache->omask)

struct cache_ent {
#define V_SNAPPY (1 << 0)
#define V_ZSTD   (1 << 1)
    unsigned short flags;
    unsigned short mtime;
    unsigned short atime;
    unsigned short pad;
};

// Values with compressed size larger than this will be backed by fs.
#define MAX_DB_VAL_SIZE (32 << 10)

struct cache {
    // common
    int dirfd;
    unsigned umask, omask;
    unsigned short now;
    // db
    DB_ENV *env;
    DB *db;
    sigset_t bset, oset;
    int pid;
};

#pragma GCC visibility push(hidden)

bool qafs_get(struct cache *cache,
	const unsigned char *sha1,
	void **valp, int *valsizep);
void qafs_unget(void *val, int valsize);
void qafs_put(struct cache *cache,
	const unsigned char *sha1,
	const void *val, int valsize);
void qafs_clean(struct cache *cache, int days);

bool qadb_open(struct cache *cache, const char *dir);
bool qadb_get(struct cache *cache,
	const void *key, int keysize,
	struct cache_ent *vent, int *ventsize);
void qadb_put(struct cache *cache,
	const void *key, int keysize,
	struct cache_ent *vent, int ventsize);
void qadb_del(struct cache *cache,
	const void *key, int keysize);
void qadb_close(struct cache *cache);
void qadb_clean(struct cache *cache, int days);

#pragma GCC visibility pop
