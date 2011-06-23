#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/file.h>

#include <db.h>
#include "cache.h"

#if DB_VERSION_MAJOR < 4 || (DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR < 4)
#error "berkeley db 4.4+ required"
#endif

struct cache {
    DB_ENV *env;
    DB *db;
    int dirfd;
    sigset_t bset, oset;
    unsigned umask, omask;
    unsigned short now;
};

#define ERROR(fmt, args...) \
    fprintf(stderr, "%s: %s: " fmt "\n", \
	    program_invocation_short_name, __func__, ##args)

#define BLOCK_SIGNALS(cache) \
    if (sigprocmask(SIG_BLOCK, &cache->bset, &cache->oset)) \
	ERROR("SIG_BLOCK: %m")
#define UNBLOCK_SIGNALS(cache) \
    if (sigprocmask(SIG_SETMASK, &cache->oset, NULL)) \
	ERROR("SIG_SETMASK: %m")

#define LOCK_DIR(cache, op) \
    {	int rc_; \
	do \
	    rc_ = flock(cache->dirfd, op); \
	while (rc_ < 0 && errno == EINTR); \
	if (rc_) \
	    ERROR("%s: %m", #op); \
    }
#define UNLOCK_DIR(cache) \
    if (flock(cache->dirfd, LOCK_UN)) \
	ERROR("LOCK_UN: %m")

#define SET_UMASK(cache) \
    cache->omask = umask(cache->umask)
#define UNSET_UMASK(cache) \
    if (cache->omask != cache->umask) \
	umask(cache->omask)

static
void errcall(const DB_ENV *env, const char *prefix, const char *msg)
{
    (void) env;
    (void) prefix;
    ERROR("%s", msg);
}

static
void msgcall(const DB_ENV *env, const char *msg)
{
    (void) env;
    ERROR("%s", msg);
}

#include <openssl/sha.h>

// All user keys are hashed with sha1, and sha1 sum is then used as db key.
// To avoid double hashing, we also use part of sha1 sum as internal db hash.
static
unsigned h_hash(DB *db, const void *key, unsigned keysize)
{
    (void) db;
    if (keysize == 20)
	return *(unsigned *) key;

    // handle CHARKEY test string and possibly other data
    unsigned char sha1[20] __attribute__((aligned(4)));
    SHA1(key, keysize, sha1);
    return *(unsigned *) sha1;
}

struct cache *cache_open(const char *dir)
{
    int rc;

    // allocate cache
    struct cache *cache = malloc(sizeof(*cache) + strlen(dir));
    if (cache == NULL) {
	ERROR("malloc: %m");
	return NULL;
    }

    // initialize signals which we will block
    sigemptyset(&cache->bset);
    sigaddset(&cache->bset, SIGHUP);
    sigaddset(&cache->bset, SIGINT);
    sigaddset(&cache->bset, SIGQUIT);
    sigaddset(&cache->bset, SIGPIPE);
    sigaddset(&cache->bset, SIGTERM);

    // initialize timestamp
    cache->now = time(NULL) / 3600 / 24;

    // allocate env
    rc = db_env_create(&cache->env, 0);
    if (rc) {
	ERROR("env_create: %s", db_strerror(rc));
	free(cache);
	return NULL;
    }

    // configure env
    cache->env->set_errcall(cache->env, errcall);
    cache->env->set_msgcall(cache->env, msgcall);
    cache->env->set_cachesize(cache->env, 0, 1 << 20, 1);

    // open dir
    cache->dirfd = open(dir, O_RDONLY | O_DIRECTORY);
    if (cache->dirfd < 0) {
	// probably ENOENT
	ERROR("%s: %m", dir);
	cache->env->close(cache->env, 0);
	free(cache);
	return NULL;
    }

    // initialize cache umask
    struct stat st;
    rc = fstat(cache->dirfd, &st);
    if (rc < 0) {
	ERROR("fstat: %m");
	st.st_mode = 0755;
    }
    cache->umask = (~st.st_mode & 022);

    // enter ciritical section
    LOCK_DIR(cache, LOCK_EX);
    BLOCK_SIGNALS(cache);
    SET_UMASK(cache);

    // open env
    rc = (cache->env->open)(cache->env, dir,
	    DB_CREATE | DB_INIT_MPOOL, 0666);
    if (rc) {
	ERROR("env_open %s: %s", dir, db_strerror(rc));
    undo:
	// env->close combines both close() and free()
	// in undo, we should close while in critical section
	cache->env->close(cache->env, 0);
	// leave critical section
	UNSET_UMASK(cache);
	UNBLOCK_SIGNALS(cache);
	UNLOCK_DIR(cache);
	// clean up and return
	close(cache->dirfd);
	free(cache);
	return NULL;
    }

    // allocate db
    rc = db_create(&cache->db, cache->env, 0);
    if (rc) {
	ERROR("db_create: %s", db_strerror(rc));
	goto undo;
    }

    // configure db
    cache->db->set_h_hash(cache->db, h_hash);

    // open db - this is the final goal
    rc = cache->db->open(cache->db, NULL, "cache.db", NULL,
	    DB_HASH, DB_CREATE, 0666);
    if (rc) {
	ERROR("db_open: %s", db_strerror(rc));
	cache->db->close(cache->db, 0);
	goto undo;
    }

    // leave critical section
    UNSET_UMASK(cache);
    UNBLOCK_SIGNALS(cache);
    UNLOCK_DIR(cache);

    return cache;
}

void cache_close(struct cache *cache)
{
    if (cache == NULL)
	return;

    int rc;

    LOCK_DIR(cache, LOCK_EX);
    BLOCK_SIGNALS(cache);

    // close db
    rc = cache->db->close(cache->db, 0);
    if (rc)
	ERROR("db_close: %s", db_strerror(rc));

    // close env
    rc = cache->env->close(cache->env, 0);
    if (rc)
	ERROR("env_close: %s", db_strerror(rc));

    UNBLOCK_SIGNALS(cache);
    UNLOCK_DIR(cache);

    close(cache->dirfd);
    free(cache);
}

// Convert 20-byte sha1 to "XX/YYY..." filename.
static
void sha1_filename(const unsigned char *sha1, char *fname, bool tmp)
{
    static const char hex[] = "0123456789abcdef";
    inline
    void sha1_byte()
    {
	*fname++ = hex[*sha1 & 0x0f];
	*fname++ = hex[*sha1++ >> 4];
    }
    sha1_byte();
    *fname++ = '/';
    int i;
    for (i = 1; i < 20; i++)
	sha1_byte();
    if (tmp) {
	*fname++ = '.';
	int rnd = rand();
	sha1 = (const unsigned char *) &rnd;
	for (i = 0; i < 4; i++)
	    sha1_byte();
    }
    *fname = '\0';
}

// Fast compression and decompression from Google.
#include <snappy-c.h>

// Values with compressed size larger than this will be backed by fs.
#define MAX_DB_VAL_SIZE (64 << 10)

struct cache_ent {
#define V_SNAPPY (1 << 0)
    unsigned short flags;
    unsigned short mtime;
    unsigned short atime;
    unsigned short pad;
};

// Will use mmap for fs get and fs put.
#include <sys/mman.h>

bool cache_get(struct cache *cache,
	const void *key, int keysize,
	void **valp, int *valsizep)
{
    if (valp)
	*valp = NULL;
    if (valsizep)
	*valsizep = 0;

    unsigned char sha1[20] __attribute__((aligned(4)));
    SHA1(key, keysize, sha1);
    DBT k = { sha1, 20 };

    char vbuf[sizeof(struct cache_ent) + MAX_DB_VAL_SIZE] __attribute__((aligned(4)));
    DBT v = { vbuf, 0 };
    v.ulen = sizeof(vbuf);
    v.flags |= DB_DBT_USERMEM;

    struct cache_ent *vent = (void *) vbuf;
    int ventsize = 0;

    // read lock
    LOCK_DIR(cache, LOCK_SH);

    // db->get can trigger mpool->put
    BLOCK_SIGNALS(cache);

    int rc = cache->db->get(cache->db, NULL, &k, &v, 0);

    UNBLOCK_SIGNALS(cache);
    UNLOCK_DIR(cache);

    if (rc == 0)
	ventsize = v.size;

    if (rc && rc != DB_NOTFOUND)
	ERROR("db_get: %s", db_strerror(rc));

    if (rc == 0 && vent->atime < cache->now) {
	// TODO: update atime
    }

    if (rc) {
	// fs get
	char fname[42];
	sha1_filename(sha1, fname, false);
	int fd = openat(cache->dirfd, fname, O_RDONLY);
	if (fd < 0) {
	    if (errno != ENOENT)
		ERROR("openat: %m");
	    return false;
	}
	struct stat st;
	rc = fstat(fd, &st);
	if (rc < 0) {
	    ERROR("fstat: %m");
	    return false;
	}
	ventsize = st.st_size;
	vent = mmap(NULL, ventsize, PROT_READ, MAP_SHARED, fd, 0);
	if (vent == MAP_FAILED) {
	    ERROR("mmap: %m");
	    close(fd);
	    return false;
	}
	close(fd);
    }

    // validate
    if (ventsize < sizeof(*vent)) {
	ERROR("vent too small");
    munmap:
	if (vent != (void *) vbuf) {
	    rc = munmap(vent, ventsize);
	    if (rc < 0)
		ERROR("munmap: %m");
	}
	return false;
    }

    // prepare for return
    if (vent->flags & V_SNAPPY) {
	// uncompress
	int csize = ventsize - sizeof(*vent);
	size_t usize;
	if (snappy_uncompressed_length(vent + 1, csize, &usize)) {
	    ERROR("snappy_uncompressed_length: invalid data");
	    goto munmap;
	}
	if (valp) {
	    if ((*valp = malloc(usize)) == NULL) {
		ERROR("malloc: %m");
		goto munmap;
	    }
	    if (snappy_uncompress(vent + 1, csize, *valp, &usize)) {
		ERROR("snappy_uncompress: invalid data");
		free(*valp);
		*valp = NULL;
		goto munmap;
	    }
	}
	else {
	    if (snappy_validate_compressed_buffer(vent + 1, csize)) {
		ERROR("snappy_validate_compressed_buffer: invalid data");
		goto munmap;
	    }
	}
	if (valsizep)
	    *valsizep = usize;
    }
    else {
	int size = ventsize - sizeof(*vent);
	if (valp) {
	    if ((*valp = malloc(size)) == NULL) {
		ERROR("malloc: %m");
		goto munmap;
	    }
	    memcpy(*valp, vent + 1, size);
	}
	if (valsizep)
	    *valsizep = size;
    }

    if (vent != (void *) vbuf) {
	rc = munmap(vent, ventsize);
	if (rc < 0)
	    ERROR("munmap: %m");
    }

    return true;
}

void cache_put(struct cache *cache,
	const void *key, int keysize,
	const void *val, int valsize)
{
    int rc;

    unsigned char sha1[20] __attribute__((aligned(4)));
    SHA1(key, keysize, sha1);

    int max_csize = snappy_max_compressed_length(valsize);
    struct cache_ent *vent = malloc(sizeof(*vent) + max_csize);
    if (vent == NULL) {
	ERROR("malloc: %m");
	return;
    }
    vent->flags = 0;
    vent->atime = 0;
    vent->mtime = 0;
    vent->pad = 0;
    int ventsize;
    size_t csize = max_csize;
    if (snappy_compress(val, valsize, vent + 1, &csize)) {
	ERROR("snappy_compress: error");
    uncompressed:
	memcpy(vent + 1, val, valsize);
	ventsize = sizeof(*vent) + valsize;
    }
    else if (csize >= valsize)
	goto uncompressed;
    else {
	vent->flags |= V_SNAPPY;
	ventsize = sizeof(*vent) + csize;
    }

    if (ventsize - sizeof(*vent) <= MAX_DB_VAL_SIZE) {
	// db put
	DBT k = { sha1, 20 };
	DBT v = { vent, ventsize };
	vent->mtime = cache->now;
	vent->atime = cache->now;

	LOCK_DIR(cache, LOCK_EX);
	BLOCK_SIGNALS(cache);

	rc = cache->db->put(cache->db, NULL, &k, &v, 0);

	UNBLOCK_SIGNALS(cache);
	UNLOCK_DIR(cache);

	if (rc)
	    ERROR("db_put: %s", db_strerror(rc));

	free(vent);
	return;
    }

    // fs put: open tmp file
    char fname[51];
    sha1_filename(sha1, fname, true);
    fname[2] = '\0';
    SET_UMASK(cache);
    rc = mkdirat(cache->dirfd, fname, 0777);
    if (rc < 0 && errno != EEXIST)
	ERROR("mkdirat: %m");
    fname[2] = '/';
    int fd = openat(cache->dirfd, fname, O_RDWR | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
	ERROR("openat: %m");
	UNSET_UMASK(cache);
	free(vent);
	return;
    }
    UNSET_UMASK(cache);

    // extend and mmap for write
    rc = ftruncate(fd, ventsize);
    if (rc < 0) {
	ERROR("ftruncate: %m");
	close(fd);
	free(vent);
	return;
    }
    void *dest = mmap(NULL, ventsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (dest == MAP_FAILED) {
	ERROR("mmap: %m");
	close(fd);
	free(vent);
	return;
    }
    close(fd);

    // write data
    memcpy(dest, vent, ventsize);
    rc = munmap(dest, ventsize);
    if (rc < 0)
	ERROR("munmap: %m");
    free(vent);

    // move to permanent location
    char outfname[42];
    memcpy(outfname, fname, 41);
    outfname[41] = '\0';
    rc = renameat(cache->dirfd, fname, cache->dirfd, outfname);
    if (rc < 0)
	ERROR("renameat: %m");
}

// ex:ts=8 sts=4 sw=4 noet
