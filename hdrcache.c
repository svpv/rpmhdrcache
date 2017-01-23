#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <rpm/rpmlib.h>
#include <lzo/lzo1x.h>
#include "hdrcache.h"
#include "mcdb.h"

struct ctx {
    struct mcdb *db;
    int initialized;
    int max_item_size;
};

static __thread
struct ctx thr_ctx;

static
void finalize(int rc, void *arg)
{
    (void) rc;
    struct ctx *ctx = arg;
    mcdb_close(ctx->db);
}

static inline
const char *opt_(const char *name)
{
    const char *str = getenv(name);
    return (str && *str) ? str : NULL;
}

#define opt(name) opt_("RPMHDRMEMCACHE_" name)

static
struct ctx *initialize()
{
    struct ctx *ctx = &thr_ctx;
    if (ctx->initialized)
	return ctx->initialized > 0 ? ctx : NULL;
    if (opt("DISABLE")) {
	ctx->initialized = -1;
	return NULL;
    }
    const char *configstring = opt("CONFIGSTRING");
    if (configstring == NULL)
	configstring = "--SERVER=localhost";
    ctx->db = mcdb_open(configstring);
    if (ctx->db == NULL) {
	ctx->initialized = -1;
	return NULL;
    }
    ctx->max_item_size = mcdb_max_item_size(ctx->db);
    if (ctx->max_item_size < 0) {
	mcdb_close(ctx->db);
	ctx->initialized = -1;
	return NULL;
    }
    assert(ctx->max_item_size > 0);
    ctx->initialized = 1;
    on_exit(finalize, ctx);
    lzo_init();
    return ctx;
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

Header hdrcache_get(const struct key *key, unsigned *off)
{
    struct ctx *ctx = initialize();
    if (ctx == NULL)
	return NULL;
    struct cache_ent *ent;
    size_t entsize;
    if (!mcdb_get(ctx->db, key->str, key->len, (const void **) &ent, &entsize))
	return NULL;
    void *blob = ent->blob;
    char ublob[hdrsize_max];
    if (ent->compressed) {
	int blobsize = entsize - sizeof(struct cache_ent);
	lzo_uint ublobsize = 0;
	int rc = lzo1x_decompress(blob, blobsize, ublob, &ublobsize, NULL);
	if (rc != LZO_E_OK || ublobsize < 1 || ublobsize > hdrsize_max) {
	    fprintf(stderr, "%s %s: lzo1x_decompress failed\n", __func__, key->str);
	    return NULL;
	}
	blob = ublob;
    }
    Header h = headerCopyLoad(blob);
    if (h == NULL) {
	fprintf(stderr, "%s %s: headerLoad failed\n", __func__, key->str);
	return NULL;
    }
    if (off)
	*off = ent->off;
    free(ent);
    return h;
}

void hdrcache_put(const struct key *key, Header h, unsigned off)
{
    struct ctx *ctx = initialize();
    if (ctx == NULL)
	return;
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
	fprintf(stderr, "%s %s: headerLoad failed\n", __func__, key->str);
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
    mcdb_put(ctx->db, key->str, key->len, ent, entsize);
}
