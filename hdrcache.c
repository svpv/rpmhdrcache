#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <rpm/rpmlib.h>
#include <lzo/lzo1x.h>
#include "hdrcache.h"
#include "mcdb.h"

static __thread
struct mcdbenv *env;

static
void finalize()
{
    mcdbenv_close(env);
}

static inline
const char *opt_(const char *name)
{
    const char *str = getenv(name);
    return (str && *str) ? str : NULL;
}

#define opt(name) opt_("RPMHDRMEMCACHE_" name)

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
    const char *configstring = opt("CONFIGSTRING");
    if (configstring == NULL)
	configstring = "--SERVER=localhost";
    env = mcdbenv_open(configstring);
    if (env == NULL) {
	initialized = -1;
	return initialized;
    }
    initialized = 1;
    atexit(finalize);
    lzo_init();
    return initialized;
}

#include <stdio.h>

static
int make_key(const char *path, const struct stat *st, char *key)
{
    const char *bn = strrchr(path, '/');
    bn = bn ? (bn + 1) : path;
    sprintf(key, "%s|%lu|%ld", bn, st->st_size, st->st_mtime);
    return strlen(key);
}

struct cache_ent {
    unsigned off;
    bool compressed;
    char blob[];
};

// The maximum size of a value in memcached is limited to 1MiB.
// Compression helps, but we choose not to depend on it for now.
static
const int hdrsize_max = (1 << 20) - sizeof(struct cache_ent);

Header hdrcache_get(const char *path, const struct stat *st, unsigned *off)
{
    if (initialize() < 0)
	return NULL;
    char key[4096];
    int keysize = make_key(path, st, key);
    struct cache_ent *ent;
    size_t entsize;
    if (!mcdb_get(env, key, keysize, (const void **) &ent, &entsize))
	return NULL;
    void *blob = ent->blob;
    char ublob[hdrsize_max];
    if (ent->compressed) {
	int blobsize = entsize - sizeof(struct cache_ent);
	lzo_uint ublobsize = 0;
	int rc = lzo1x_decompress(blob, blobsize, ublob, &ublobsize, NULL);
	if (rc != LZO_E_OK || ublobsize < 1 || ublobsize > hdrsize_max) {
	    fprintf(stderr, "%s %s: lzo1x_decompress failed\n", __func__, key);
	    return NULL;
	}
	blob = ublob;
    }
    Header h = headerCopyLoad(blob);
    if (h == NULL) {
	fprintf(stderr, "%s %s: headerLoad failed\n", __func__, key);
	return NULL;
    }
    if (off)
	*off = ent->off;
    free(ent);
    return h;
}

void hdrcache_put(const char *path, const struct stat *st, Header h, unsigned off)
{
    if (initialize() < 0)
	return;
    char key[4096];
    int keysize = make_key(path, st, key);
    int hdrsize = headerSizeof(h, HEADER_MAGIC_NO);
    if (hdrsize < 1 || hdrsize > hdrsize_max)
	return;
    int entbufsize = sizeof(struct cache_ent) + hdrsize +
	    hdrsize / 16 + 64 + 3; // max lzo overhead
    char entbuf[entbufsize] __attribute__((aligned(4)));
    int entsize;
    struct cache_ent *ent = (void *) entbuf;
    ent->off = off;
    void *blob = headerUnload(h);
    if (blob == NULL) {
	fprintf(stderr, "%s %s: headerLoad failed\n", __func__, key);
	return;
    }
    char lzobuf[LZO1X_1_MEM_COMPRESS];
    lzo_uint lzosize = 0;
    lzo1x_1_compress(blob, hdrsize, ent->blob, &lzosize, lzobuf);
    if (lzosize > 0 && lzosize < hdrsize) {
	ent->compressed = 1;
	entsize = sizeof(struct cache_ent) + lzosize;
    }
    else {
	ent->compressed = 0;
	memcpy(ent->blob, blob, hdrsize);
	entsize = sizeof(struct cache_ent) + hdrsize;
    }
    free(blob);
    mcdb_put(env, key, keysize, ent, entsize);
}

// ex: set ts=8 sts=4 sw=4 noet:
