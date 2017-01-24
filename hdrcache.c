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
    if (ctx->max_item_size < 8192) {
	if (ctx->max_item_size > 0)
	    fprintf(stderr, "%s: %s: memcached max item size too small: %d\n",
		    program_invocation_short_name, "rpmhdrcache", ctx->max_item_size);
	mcdb_close(ctx->db);
	ctx->initialized = -1;
	return NULL;
    }
    assert(ctx->max_item_size > 0);
    ctx->initialized = 1;
    on_exit(finalize, ctx);
    return ctx;
}

struct cache_ent {
    unsigned off;	// file offset after rpmReadPackageFile
    unsigned blobsize;	// uncompressed size (LZ4 cannot provide)
    char zblob[];	// compressed blob
};

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

Header hdrcache_get(const struct key *key, unsigned *off)
{
    struct ctx *ctx = initialize();
    if (ctx == NULL)
	return NULL;
    struct cache_ent *ent;
    size_t entsize;
    if (!mcdb_get(ctx->db, key->str, key->len, (const void **) &ent, &entsize))
	return NULL;
    if (entsize < ENTSIZE_MIN) {
	fprintf(stderr, "%s %s: bad entry\n", __func__, key->str);
noent:	free(ent);
	return NULL;
    }
    if (ent->blobsize < HDRSIZE_MIN || ent->blobsize > HDRSIZE_MAX) {
	fprintf(stderr, "%s %s: bad ent->blobsize\n", __func__, key->str);
	goto noent;
    }
    void *blob = malloc(ent->blobsize);
    if (blob == NULL) {
	fprintf(stderr, "%s %s: malloc: %m\n", __func__, key->str);
	goto noent;
    }
    int blobsize = LZ4_decompress_safe(ent->zblob, blob, entsize - sizeof(*ent), ent->blobsize);
    if (blobsize != (int) ent->blobsize) {
	fprintf(stderr, "%s %s: %s failed\n", __func__, key->str, "LZ4_decompress_safe");
	free(blob);
	goto noent;
    }
    Header h = headerImport(blob, blobsize, HEADERIMPORT_FAST);
    if (h == NULL) {
	fprintf(stderr, "%s %s: %s failed\n", __func__, key->str, "headerLoad");
	free(blob);
	goto noent;
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
    int blobsize = headerSizeof(h, HEADER_MAGIC_NO);
    if (blobsize < HDRSIZE_MIN || blobsize > HDRSIZE_MAX)
	return;
    // Assume that LZ4 can compress by a factor of 2.
    // The compressed header then must not exceed max_item_size.
    if (blobsize / 2 > ctx->max_item_size)
	return;
    int entsize = sizeof(struct cache_ent) + LZ4_compressBound(blobsize);
    if (entsize > ctx->max_item_size)
	entsize = ctx->max_item_size;
    struct cache_ent *ent = malloc(entsize);
    if (ent == NULL) {
	fprintf(stderr, "%s %s: malloc: %m\n", __func__, key->str);
	return;
    }
    void *blob = headerUnload(h);
    if (blob == NULL) {
	fprintf(stderr, "%s %s: %s failed\n", __func__, key->str, "headerLoad");
	return;
    }
    int zblobsize = LZ4_compress_default(blob, ent->zblob, blobsize, entsize - sizeof(*ent));
    free(blob);
    if (zblobsize < (int) ENTSIZE_MIN - (int) sizeof(*ent)) {
	fprintf(stderr, "%s %s: %s failed\n", __func__, key->str, "LZ4_compress_default");
	free(ent);
	return;
    }
    assert(zblobsize <= entsize - (int) sizeof(*ent));
    entsize = zblobsize + sizeof(*ent);
    ent->off = off;
    ent->blobsize = blobsize;
    mcdb_put(ctx->db, key->str, key->len, ent, entsize);
    free(ent);
}
