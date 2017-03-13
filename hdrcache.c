#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <rpm/rpmlib.h>
#include <lz4.h>
#include "hdrcache.h"
#include "mcdb.h"

struct ctx {
    struct rpmcache *rpmcache;
    int initialized;
};

static __thread
struct ctx thr_ctx;

static
void finalize(int rc, void *arg)
{
    (void) rc;
    struct ctx *ctx = arg;
    rpmcache_close(ctx->rpmcache);
}

static
struct ctx *initialize()
{
    struct ctx *ctx = &thr_ctx;
    if (ctx->initialized)
	return ctx->initialized > 0 ? ctx : NULL;
    ctx->rpmcache = rpmcache_open("rpmhdrcache");
    if (ctx->rpmcache == NULL) {
	ctx->initialized = -1;
	return NULL;
    }
    ctx->initialized = 1;
    on_exit(finalize, ctx);
    return ctx;
}

// Any valid rpm header must provide at least the number of its
// index entries and the size of its data.
#define HDRSIZE_MIN 8
// The maximum size of RPM header which we even try to deal with.
// Note that the default size limit in memcached is 1MiB, but it
// can be increased to 128MiB.  Thus the uncompressed size of
// a header is limited to 256MiB.  This is also the limit imposed
// in recent rpm releases.
#define HDRSIZE_MAX (1 << 28)

// Assume that LZ4 cannot compress an 8-byte header.
#define ENTSIZE_MIN (sizeof(struct cache_ent) + HDRSIZE_MIN)

Header hdrcache_get(const struct rpmkey *key, unsigned *off)
{
    struct ctx *ctx = initialize();
    if (ctx == NULL)
	return NULL;
    void *blob;
    int blobsize;
    if (!rpmcache_get(ctx->rpmcache, key, &blob, &blobsize))
	return NULL;
    Header h = headerImport(blob, blobsize - 4, HEADERIMPORT_FAST);
    if (h == NULL) {
	fprintf(stderr, "%s %s: %s failed\n", __func__, key->str, "headerLoad");
	free(blob);
	return NULL;
    }
    if (off)
	memcpy(off, blob + blobsize - 4, 4);
    return h;
}

void hdrcache_put(const struct rpmkey *key, Header h, unsigned off)
{
    struct ctx *ctx = initialize();
    if (ctx == NULL)
	return;
    int blobsize = headerSizeof(h, HEADER_MAGIC_NO);
    if (blobsize < HDRSIZE_MIN || blobsize > HDRSIZE_MAX)
	return;
    void *blob = headerUnload(h);
    blob = realloc(blob, blobsize + 4);
    memcpy(blob + blobsize, &off, 4);
    rpmcache_put(ctx->rpmcache, key, blob, blobsize + 4);
}
