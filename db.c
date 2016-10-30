#include "cache-impl.h"

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

#include <sys/file.h>

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

#define BLOCK_SIGNALS(cache) \
    if (sigprocmask(SIG_BLOCK, &cache->bset, &cache->oset)) \
	ERROR("SIG_BLOCK: %m")
#define UNBLOCK_SIGNALS(cache) \
    if (sigprocmask(SIG_SETMASK, &cache->oset, NULL)) \
	ERROR("SIG_SETMASK: %m")

bool qadb_open(struct cache *cache, const char *dir)
{
    // initialize signals which we will block
    sigemptyset(&cache->bset);
    sigaddset(&cache->bset, SIGHUP);
    sigaddset(&cache->bset, SIGINT);
    sigaddset(&cache->bset, SIGQUIT);
    sigaddset(&cache->bset, SIGPIPE);
    sigaddset(&cache->bset, SIGTERM);

    // remember our process
    cache->pid = getpid();

    // allocate env
    int rc = db_env_create(&cache->env, 0);
    if (rc) {
	ERROR("env_create: %s", db_strerror(rc));
	return false;
    }

    // configure env
    cache->env->set_errcall(cache->env, errcall);
    cache->env->set_msgcall(cache->env, msgcall);
    cache->env->set_cachesize(cache->env, 0, 1 << 20, 1);

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
	return false;
    }

    // allocate db
    rc = db_create(&cache->db, cache->env, 0);
    if (rc) {
	ERROR("db_create: %s", db_strerror(rc));
	goto undo;
    }

    // open db
    rc = cache->db->open(cache->db, NULL, "cache.db", NULL,
	    DB_BTREE, DB_CREATE, 0666);
    if (rc) {
	ERROR("db_open: %s", db_strerror(rc));
	cache->db->close(cache->db, 0);
	goto undo;
    }

    // leave critical section
    UNSET_UMASK(cache);
    UNBLOCK_SIGNALS(cache);
    UNLOCK_DIR(cache);
    return true;
}

void qadb_close(struct cache *cache)
{
    // don't close after fork
    if (cache->pid != getpid())
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
}

bool qadb_get(struct cache *cache,
	const void *key, int keysize,
	struct cache_ent *vent, int *ventsize)
{
    DBT k = {
	.data = key,
	.size = keysize,
    };
    DBT v = {
	.data = vent,
	.ulen = *ventsize,
	.flags = DB_DBT_USERMEM,
    };

    // read lock
    LOCK_DIR(cache, LOCK_SH);

    // db->get can trigger mpool->put
    BLOCK_SIGNALS(cache);

    int rc = cache->db->get(cache->db, NULL, &k, &v, 0);

    if (rc) {
	UNBLOCK_SIGNALS(cache);
	UNLOCK_DIR(cache);
	if (rc != DB_NOTFOUND)
	    ERROR("db_get: %s", db_strerror(rc));
	return false;
    }

    // sucessful return
    *ventsize = v.size;

    // update atime
    if (v.size >= sizeof(*vent) && vent->atime < cache->now) {
	v.size = sizeof(*vent);
	v.dlen = sizeof(*vent);
	v.flags |= DB_DBT_PARTIAL;
	vent->atime = cache->now;
	rc = cache->db->put(cache->db, NULL, &k, &v, 0);
    }

    UNBLOCK_SIGNALS(cache);
    UNLOCK_DIR(cache);

    if (rc)
	ERROR("db_put: %s", db_strerror(rc));

    return true;
}

void qadb_put(struct cache *cache,
	const void *key, int keysize,
	struct cache_ent *vent, int ventsize)
{
    DBT k = {
	.data = key,
	.size = keysize,
    };
    DBT v = {
	.data = vent,
	.size = ventsize,
    };
    vent->mtime = cache->now;
    vent->atime = cache->now;

    LOCK_DIR(cache, LOCK_EX);
    BLOCK_SIGNALS(cache);

    int rc = cache->db->put(cache->db, NULL, &k, &v, 0);

    UNBLOCK_SIGNALS(cache);
    UNLOCK_DIR(cache);

    if (rc)
	ERROR("db_put: %s", db_strerror(rc));
}

void qadb_del(struct cache *cache,
	const void *key, int keysize)
{
    DBT k = {
	.data = key,
	.size = keysize,
    };

    LOCK_DIR(cache, LOCK_EX);
    BLOCK_SIGNALS(cache);

    int rc = cache->db->del(cache->db, NULL, &k, 0);

    UNBLOCK_SIGNALS(cache);
    UNLOCK_DIR(cache);

    if (rc && rc != DB_NOTFOUND)
	ERROR("db_del: %s", db_strerror(rc));
}

void qadb_clean(struct cache *cache, int days)
{
    LOCK_DIR(cache, LOCK_EX);

    DBC *dbc;
    BLOCK_SIGNALS(cache);
    int rc = cache->db->cursor(cache->db, NULL, &dbc, 0);
    UNBLOCK_SIGNALS(cache);

    if (rc) {
	UNLOCK_DIR(cache);
	ERROR("db_cursor: %s", db_strerror(rc));
	return;
    }

    while (1) {
	unsigned char sha1[20] __attribute__((aligned(4)));
	DBT k = {
	    .data = sha1,
	    .ulen = sizeof(sha1),
	    .flags = DB_DBT_USERMEM,
	};

	struct cache_ent vbuf;
	struct cache_ent *vent = &vbuf;
	DBT v = {
	    .data = &vbuf,
	    .ulen = sizeof(vbuf),
	    .dlen = sizeof(vbuf),
	    .flags = DB_DBT_USERMEM | DB_DBT_PARTIAL,
	};

	BLOCK_SIGNALS(cache);
	rc = dbc->get(dbc, &k, &v, DB_NEXT);
	UNBLOCK_SIGNALS(cache);

	if (rc) {
	    if (rc != DB_NOTFOUND)
		ERROR("dbc_get: %s", db_strerror(rc));
	    break;
	}

	if (k.size < sizeof(sha1)) {
	    ERROR("sha1 too small");
	    continue;
	}

	if (v.size < sizeof(vent)) {
	    ERROR("vent too small");
	    continue;
	}

	if (vent->mtime + days >= cache->now) continue;
	if (vent->atime + days >= cache->now) continue;

	BLOCK_SIGNALS(cache);
	rc = dbc->del(dbc, 0);
	UNBLOCK_SIGNALS(cache);

	if (rc)
	    ERROR("dbc_del: %s", db_strerror(rc));
    }

    BLOCK_SIGNALS(cache);
    rc = dbc->close(dbc);
    UNBLOCK_SIGNALS(cache);

    if (rc)
	ERROR("dbc_close: %s", db_strerror(rc));

    UNLOCK_DIR(cache);
}
