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

bool cache_get(struct cache *cache,
	const void *key, int keysize,
	void **valp, int *valsizep)
{
    DBT k = { (void *) key, keysize };
    DBT v = { NULL, 0 };
    v.flags = DB_DBT_MALLOC;

    // read lock
    LOCK_DIR(cache, LOCK_SH);

    // db->get can trigger mpool->put
    BLOCK_SIGNALS(cache);

    int rc = cache->db->get(cache->db, NULL, &k, &v, 0);

    UNBLOCK_SIGNALS(cache);
    UNLOCK_DIR(cache);

    if (rc) {
	if (rc != DB_NOTFOUND)
	    ERROR("db_get: %s", db_strerror(rc));
	return false;
    }
    if (valp)
	*valp = v.data;
    else
	free(v.data);
    if (valsizep)
	*valsizep = v.size;
    return true;
}

void cache_put(struct cache *cache,
	const void *key, int keysize,
	const void *val, int valsize)
{
    DBT k = { (void *) key, keysize };
    DBT v = { (void *) val, valsize };

    LOCK_DIR(cache, LOCK_EX);
    BLOCK_SIGNALS(cache);

    int rc = cache->db->put(cache->db, NULL, &k, &v, 0);

    UNBLOCK_SIGNALS(cache);
    UNLOCK_DIR(cache);

    if (rc)
	ERROR("db_put: %s", db_strerror(rc));
}

// ex:ts=8 sts=4 sw=4 noet
