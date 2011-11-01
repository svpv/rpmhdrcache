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

struct cache_ent {
#define V_SNAPPY (1 << 0)
    unsigned short flags;
    unsigned short mtime;
    unsigned short atime;
    unsigned short pad;
};

// Values with compressed size larger than this will be backed by fs.
#define MAX_DB_VAL_SIZE (32 << 10)

struct cache {
    DB_ENV *env;
    DB *db;
    int dirfd;
    int pid;
    sigset_t bset, oset;
    unsigned umask, omask;
    unsigned short now;
    unsigned char sha1[20] __attribute__((aligned(4)));
    struct cache_ent *vent;
    int ventsize;
    char vbuf[sizeof(struct cache_ent) + MAX_DB_VAL_SIZE] __attribute__((aligned(4)));
};

#pragma GCC visibility push(hidden)

bool fs_get(struct cache *cache);
void fs_unget(struct cache *cache);
void fs_put(struct cache *cache);
void fs_clean(struct cache *cache, int days);

#pragma GCC visibility pop
