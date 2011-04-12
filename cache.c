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
    unsigned umask;
    char dirname[1];
};

#define ERROR(fmt, args...) \
    fprintf(stderr, "%s: %s: " fmt "\n", \
	    program_invocation_short_name, __func__, ##args)

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

static
#if DB_VERSION_MAJOR > 4 || (DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR >= 5)
int isalive(DB_ENV *env, pid_t pid, db_threadid_t tid, u_int32_t flags)
#else
int isalive(DB_ENV *env, pid_t pid, db_threadid_t tid)
#endif
{
    (void) env;
    (void) tid;
#if DB_VERSION_MAJOR > 4 || (DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR >= 5)
    (void) flags;
#endif
    int rc = kill(pid, 0);
    if (rc == 0)
	return 1;
    if (errno == ESRCH)
	return 0;
    return 1;
}

struct cache *cache_open(const char *dir)
{
    int rc;

    // 1: allocate cache
    struct cache *cache = malloc(sizeof(*cache) + strlen(dir));
    if (cache == NULL) {
	ERROR("malloc: %m");
    undo0:
	return NULL;
    }
    strcpy(cache->dirname, dir);

    // 2: allocate env
    rc = db_env_create(&cache->env, 0);
    if (rc) {
	ERROR("env_create: %s", db_strerror(rc));
    undo1:
	free(cache);
	goto undo0;
    }

    // configure env
    cache->env->set_errcall(cache->env, errcall);
    cache->env->set_msgcall(cache->env, msgcall);
    cache->env->set_isalive(cache->env, isalive);
    cache->env->set_thread_count(cache->env, 16);

    // 3: open dir
    cache->dirfd = open(cache->dirname, O_RDONLY | O_DIRECTORY);
    if (cache->dirfd < 0) {
	ERROR("open %s: %m", cache->dirname);
    undo2:
	cache->env->close(cache->env, 0);
	goto undo1;
    }

    // initialize umask
    struct stat st;
    rc = fstat(cache->dirfd, &st);
    if (rc < 0) {
	ERROR("fstat %s: %m", cache->dirname);
    undo3:
	close(cache->dirfd);
	goto undo2;
    }
    cache->umask = (~st.st_mode & 022);

    // 4: lock dir
    do
	rc = flock(cache->dirfd, LOCK_EX);
    while (rc < 0 && errno == EINTR);
    if (rc) {
	ERROR("LOCK_EX %s: %m", cache->dirname);
	goto undo3;
    }

    // 5: block signals
    sigset_t set, oldset;
    sigfillset(&set);
    sigprocmask(SIG_BLOCK, &set, &oldset);

    // 6: adjust umask
    unsigned omask = umask(cache->umask);

    // 7: open env
    rc = (cache->env->open)(cache->env, cache->dirname,
	    DB_CREATE | DB_INIT_CDB | DB_INIT_MPOOL, 0666);
    if (rc) {
	ERROR("env_open %s: %s", cache->dirname, db_strerror(rc));
    undo654:
	if (omask != cache->umask)
	    umask(omask);
	sigprocmask(SIG_SETMASK, &oldset, NULL);
	flock(cache->dirfd, LOCK_UN);
	goto undo3;
    }

    // run recovery
    rc = cache->env->failchk(cache->env, 0);
    if (rc) {
	ERROR("env_failchk %s: %s", cache->dirname, db_strerror(rc));
    undo7:
	cache->env->close(cache->env, 0);
	goto undo654;
    }

    // 8: allocate db
    rc = db_create(&cache->db, cache->env, 0);
    if (rc) {
	ERROR("db_create: %s", db_strerror(rc));
	goto undo7;
    }

    // open db - this is the final goal
    rc = cache->db->open(cache->db, NULL, "cache.db", NULL,
	    DB_HASH, DB_CREATE, 0666);
    if (rc) {
	ERROR("db_open %s: %s", cache->dirname, db_strerror(rc));
    //undo8:
	cache->db->close(cache->db, 0);
	goto undo7;
    }

    // 6, 5, 4 - release protection
    if (omask != cache->umask)
	umask(omask);
    sigprocmask(SIG_SETMASK, &oldset, NULL);
    flock(cache->dirfd, LOCK_UN);

    return cache;
}

void cache_close(struct cache *cache)
{
    if (cache == NULL)
	return;

    int rc;

    // lock dir
    do
	rc = flock(cache->dirfd, LOCK_EX);
    while (rc < 0 && errno == EINTR);
    if (rc)
	ERROR("LOCK_EX %s: %m", cache->dirname);

    // block signals
    sigset_t set, oldset;
    sigfillset(&set);
    sigprocmask(SIG_BLOCK, &set, &oldset);

    // close db
    rc = cache->db->close(cache->db, 0);
    if (rc)
	ERROR("db_close %s: %s", cache->dirname, db_strerror(rc));

    // close env
    rc = cache->env->close(cache->env, 0);
    if (rc)
	ERROR("env_close %s: %s", cache->dirname, db_strerror(rc));

    sigprocmask(SIG_SETMASK, &oldset, NULL);
    flock(cache->dirfd, LOCK_UN);

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
    int rc = cache->db->get(cache->db, NULL, &k, &v, 0);
    if (rc) {
	if (rc != DB_NOTFOUND)
	    ERROR("db_get %s: %s", cache->dirname, db_strerror(rc));
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
    sigset_t set, oldset;
    sigfillset(&set);
    sigprocmask(SIG_BLOCK, &set, &oldset);

    DBT k = { (void *) key, keysize };
    DBT v = { (void *) val, valsize };
    int rc = cache->db->put(cache->db, NULL, &k, &v, 0);

    sigprocmask(SIG_SETMASK, &oldset, NULL);

    if (rc)
	ERROR("db_put %s: %s", cache->dirname, db_strerror(rc));
}

// ex:ts=8 sts=4 sw=4 noet
