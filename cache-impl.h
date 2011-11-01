#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <db.h>

#define ERROR(fmt, args...) \
    fprintf(stderr, "%s: %s: " fmt "\n", \
	    program_invocation_short_name, __func__, ##args)

#define SET_UMASK(cache) \
    cache->omask = umask(cache->umask)
#define UNSET_UMASK(cache) \
    if (cache->omask != cache->umask) \
	umask(cache->omask)

struct cache {
    DB_ENV *env;
    DB *db;
    int dirfd;
    int pid;
    sigset_t bset, oset;
    unsigned umask, omask;
    unsigned short now;
    unsigned char sha1[20] __attribute__((aligned(4)));
};

struct cache_ent {
#define V_SNAPPY (1 << 0)
    unsigned short flags;
    unsigned short mtime;
    unsigned short atime;
    unsigned short pad;
};

#pragma GCC visibility push(hidden)

bool fs_get(struct cache *cache,
	void **valp, int *valsizep);
void fs_unget(struct cache *cache,
	void *val, int valsize);
void fs_put(struct cache *cache,
	const void *val, int valsize);
void fs_clean(struct cache *cache, int days);

#pragma GCC visibility pop
