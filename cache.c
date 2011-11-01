#include "cache.h"
#include "cache-impl.h"

#define BLOCK_SIGNALS(cache) \
    if (sigprocmask(SIG_BLOCK, &cache->bset, &cache->oset)) \
	ERROR("SIG_BLOCK: %m")
#define UNBLOCK_SIGNALS(cache) \
    if (sigprocmask(SIG_SETMASK, &cache->oset, NULL)) \
	ERROR("SIG_SETMASK: %m")

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

    // remember our process
    cache->pid = getpid();

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

    close(cache->dirfd);
    free(cache);
}

// Fast compression and decompression from Google.
#include <snappy-c.h>

// Values with compressed size larger than this will be backed by fs.
#define MAX_DB_VAL_SIZE (32 << 10)

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

    if (rc)
	if (!fs_get(cache, sha1, (void **) &vent, &ventsize))
	    return false;

    // validate
    if (ventsize < sizeof(*vent)) {
	ERROR("vent too small");
    unget:
	if (vent != (void *) vbuf)
	    fs_unget(cache, sha1, vent, ventsize);
	return false;
    }

    // update db atime
    if (vent == (void *) vbuf && vent->atime < cache->now) {
	LOCK_DIR(cache, LOCK_EX);
	BLOCK_SIGNALS(cache);

	// partial update, user data unchanged
	v.flags |= DB_DBT_PARTIAL;
	v.dlen = sizeof(*vent);

	// the record must be still there
	rc = cache->db->get(cache->db, NULL, &k, &v, DB_GET_BOTH);
	if (rc) {
	    UNBLOCK_SIGNALS(cache);
	    UNLOCK_DIR(cache);
	    if (rc != DB_NOTFOUND)
		ERROR("db_get: %s", db_strerror(rc));
	}
	else {
	    // actual update
	    v.size = sizeof(*vent);
	    vent->atime = cache->now;
	    rc = cache->db->put(cache->db, NULL, &k, &v, 0);
	    UNBLOCK_SIGNALS(cache);
	    UNLOCK_DIR(cache);
	    if (rc)
		ERROR("db_put: %s", db_strerror(rc));
	}
    }

    // prepare for return
    if (vent->flags & V_SNAPPY) {
	// uncompress
	int csize = ventsize - sizeof(*vent);
	size_t usize;
	if (snappy_uncompressed_length(vent + 1, csize, &usize)) {
	    ERROR("snappy_uncompressed_length: invalid data");
	    goto unget;
	}
	if (valp) {
	    if ((*valp = malloc(usize + 1)) == NULL) {
		ERROR("malloc: %m");
		goto unget;
	    }
	    if (snappy_uncompress(vent + 1, csize, *valp, &usize)) {
		ERROR("snappy_uncompress: invalid data");
		free(*valp);
		*valp = NULL;
		goto unget;
	    }
	    ((char *) *valp)[usize] = '\0';
	}
	else {
	    if (snappy_validate_compressed_buffer(vent + 1, csize)) {
		ERROR("snappy_validate_compressed_buffer: invalid data");
		goto unget;
	    }
	}
	if (valsizep)
	    *valsizep = usize;
    }
    else {
	int size = ventsize - sizeof(*vent);
	if (size && valp) {
	    if ((*valp = malloc(size + 1)) == NULL) {
		ERROR("malloc: %m");
		goto unget;
	    }
	    memcpy(*valp, vent + 1, size);
	    ((char *) *valp)[size] = '\0';
	}
	if (valsizep)
	    *valsizep = size;
    }

    if (vent != (void *) vbuf)
	fs_unget(cache, sha1, vent, ventsize);

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
    else {
	// db del
	DBT k = { sha1, 20 };

	LOCK_DIR(cache, LOCK_EX);
	BLOCK_SIGNALS(cache);

	rc = cache->db->del(cache->db, NULL, &k, 0);

	UNBLOCK_SIGNALS(cache);
	UNLOCK_DIR(cache);

	if (rc && rc != DB_NOTFOUND)
	    ERROR("db_del: %s", db_strerror(rc));
    }

    fs_put(cache, sha1, vent, ventsize);
}

void cache_clean(struct cache *cache, int days)
{
    if (days < 1) {
	ERROR("days must be greater than 0, got %d", days);
	return;
    }

    int rc;

    // db clean
    LOCK_DIR(cache, LOCK_EX);

    DBC *dbc;
    BLOCK_SIGNALS(cache);
    rc = cache->db->cursor(cache->db, NULL, &dbc, 0);
    UNBLOCK_SIGNALS(cache);

    if (rc) {
	UNLOCK_DIR(cache);
	ERROR("db_cursor: %s", db_strerror(rc));
	return;
    }

    while (1) {
	unsigned char sha1[20] __attribute__((aligned(4)));
	DBT k = { sha1, 0 };
	k.ulen = sizeof(sha1);
	k.flags |= DB_DBT_USERMEM;

	struct cache_ent vbuf, *vent = &vbuf;
	DBT v = { &vbuf, 0 };
	v.ulen = sizeof(vbuf);
	v.dlen = sizeof(vbuf);
	v.flags |= DB_DBT_USERMEM;
	v.flags |= DB_DBT_PARTIAL;

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

	if (v.size < sizeof(*vent)) {
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

    fs_clean(cache, days);
}

// ex:ts=8 sts=4 sw=4 noet
