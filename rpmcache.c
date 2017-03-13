#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <lz4.h>
#include "cache.h"
#include "rpmcache.h"
#include "error.h"
#include "cache.h"
#include "mcdb.h"
#include "conf.h"

struct rpmcache {
    enum conftype t;	// the backend found in rpmcache.conf
    void *db;		// the backend's handle
    size_t max_item_size;
};

struct rpmcache *rpmcache_open(const char *name)
{
    struct conf *conf = NULL;
    const char *fname = getenv("RPMCACHE_CONFIG");
    if (fname && *fname) {
	FILE *fp = fopen(fname, "r");
	if (!fp) {
	    ERROR("fopen: %s: %m", fname);
	    return NULL;
	}
	conf = findconf(fp, name);
	fclose(fp);
    }
    else {
	// TODO: handle /etc/rpmcache.conf and ~/.config/rpmcache.conf
    }
    if (!conf) {
	ERROR("%s: cache unconfigured", name);
	return NULL;
    }

    void *db = NULL;
    int max_item_size = INT_MAX;
    switch (conf->t) {
    case CONFTYPE_QACACHE:
	db = cache_open(conf->str);
	break;
    case CONFTYPE_MEMCACHED:
	db = mcdb_open(conf->str);
	if (db) {
	    max_item_size = mcdb_max_item_size(db);
	    if (max_item_size < 1024) {
		ERROR("%s: cannot get max_item_size", name);
		mcdb_close(db);
		return NULL;
	    }
	}
	break;
    case CONFTYPE_REDIS:
	ERROR("redis not yet supported");
    }
    if (db == NULL) {
	ERROR("%s: cannot open db", name);
	return NULL;
    }

    struct rpmcache *rpmcache = malloc(sizeof(*rpmcache));
    rpmcache->t = conf->t;
    rpmcache->db = db;
    rpmcache->max_item_size = max_item_size;
    free(conf);
    return rpmcache;
}

// Cache entry format:
// - uncompressed: <blob> '\0'
// - compressed: <uncompressed-size> <lz4-blob> '\1'

bool rpmcache_get(struct rpmcache *rpmcache,
	const struct rpmkey *key,
	void **valp, int *valsizep)
{
    if (rpmcache->t == CONFTYPE_QACACHE)
	return cache_get(rpmcache->db, key->str, key->len, valp, valsizep);

    char *ent;
    size_t entsize;

    switch (rpmcache->t) {
    case CONFTYPE_QACACHE:
	assert(!"possible");
	return false;
    case CONFTYPE_MEMCACHED:
	if (!mcdb_get(rpmcache->db, key->str, key->len, (void *) &ent, &entsize))
	    return false;
	break;
    case CONFTYPE_REDIS:
	ERROR("redis not yet supported");
	return false;
    }

    // empty entries are handled specially, as in cache.h
    if (entsize == 0) {
	free(ent);
	if (valp)
	    *valp = NULL;
	if (valsizep)
	    *valsizep = 0;
	return true;
    }

    // last bytes indicates lz4 compression; thus, uncompressed
    // entries are automatically null-terminated
    if (ent[entsize-1] == '\0') {
	if (valp)
	    *valp = ent;
	if (valsizep)
	    *valsizep = entsize;
	return true;
    }

    // otherwise, the entry starts with the uncompresssed size
    if (entsize <= 5) {
	ERROR("%s: bad entry", key->str);
	free(ent);
	return false;
    }
    unsigned usize = *(unsigned *) ent;
#define MIN_COMPRESS_SIZE 33
    if (usize < MIN_COMPRESS_SIZE || usize > INT_MAX) {
	ERROR("%s: bad entry size", key->str);
	free(ent);
	return false;
    }
    if (valsizep)
	*valsizep = usize;
    if (valp == NULL) {
	free(ent);
	return true;
    }
    char *blob = malloc(usize + 1);
    if (blob == NULL) {
	ERROR("%s: malloc: %m", key->str);
	free(ent);
	return false;
    }
    int blobsize = LZ4_decompress_safe(ent + 4, blob, entsize - 5, usize);
    free(ent);
    if (blobsize != (int) usize) {
	ERROR("%s: %s failed", key->str, "LZ4_decompress_safe");
	free(blob);
	return false;
    }
    blob[blobsize] = '\0';
    *valp = blob;
    return true;
}

void rpmcache_put(struct rpmcache *rpmcache,
	const struct rpmkey *key,
	const void *val, int valsize)
{
    if (rpmcache->t == CONFTYPE_QACACHE)
	return cache_put(rpmcache->db, key->str, key->len, val, valsize);

    // Assume that LZ4 can compress by a factor of 2.
    // The compressed item then must not exceed max_item_size.
    if (valsize / 2 > rpmcache->max_item_size)
	return;

    size_t entsize;
    bool limit = false;
    if (valsize < MIN_COMPRESS_SIZE)
	entsize = valsize ? valsize + 1 : 0;
    else {
	entsize = LZ4_compressBound(valsize);
	if (entsize == 0) {
	    ERROR("%s: %s failed", key->str, "LZ4_compressBound");
	    return;
	}
	entsize += 5;
	if (entsize > rpmcache->max_item_size) {
	    entsize = rpmcache->max_item_size;
	    limit = true;
	}
    }

    char *ent = valsize ? malloc(entsize) : "";
    if (ent == NULL) {
	ERROR("%s: malloc: %m", key->str);
	return;
    }

    if (valsize < MIN_COMPRESS_SIZE) {
	if (valsize) {
uncompressed1:
	    memcpy(ent, val, valsize);
	    entsize = valsize + 1;
	    ent[entsize-1] = '\0';
	}
    }
    else {
	size_t zblobsize = LZ4_compress_default(val, ent + 4, valsize, entsize - 5);
	if (zblobsize == 0) {
	    if (!limit)
		ERROR("%s: %s failed", key->str, "LZ4_compress_default");
uncompressed2:
	    if (entsize >= valsize + 1)
		goto uncompressed1;
	    free(ent);
	    return;
	}
	// incompressible?
	if (zblobsize + 4 >= (size_t) valsize)
	    goto uncompressed2;
	*(unsigned *) ent = valsize;
	entsize = zblobsize + 5;
	ent[entsize-1] = '\1';
    }

    switch (rpmcache->t) {
    case CONFTYPE_QACACHE:
	assert(!"possible");
	break;
    case CONFTYPE_MEMCACHED:
	mcdb_put(rpmcache->db, key->str, key->len, ent, entsize);
	break;
    case CONFTYPE_REDIS:
	ERROR("redis not yet supported");
    }

    if (valsize)
	free(ent);
}

void rpmcache_close(struct rpmcache *rpmcache)
{
    cache_close((struct cache *) rpmcache);
}

// ex:ts=8 sts=4 sw=4 noet
