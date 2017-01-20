#include <assert.h>
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
#include <stdint.h>
#include <endian.h>
#include "sm3.h"

// memcached needs ASCII keys no longer than 250 characters
#define MAXKEY 250

static
int hdrcache_key(const char *bn, const struct stat *st, char *key)
{
    size_t len = strlen(bn);
    assert(len > 4);
    assert(bn[len-4] == '.');
    // size and mtime will be serialized into 8 base64 characters;
    // also, ".rpm" suffix will be stripped; on the second thought,
    // the dot should rather be kept
    size_t keylen = len + 8 - 3;
    if (keylen > MAXKEY) {
	fprintf(stderr, "%s %s: name too long\n", __func__, bn);
	return 0;
    }
    // copy basename
    memcpy(key, bn, len - 3);
    key += len - 3;
    // combine size+mtime
#if __BYTE_ORDER == __LITTLE_ENDIAN
    uint64_t sm48 = 0;
    sm3(st->st_size, st->st_mtime, (unsigned short *) &sm48);
#else
    unsigned short sm[3];
    sm3(st->st_size, st->st_mtime, sm);
    uint64_t sm48 = htole16(sm[0]) |
	    // when htole16(x) returns unsigned short, sm[1] will be
	    // promoted to int on behalf of << operator; it is crucial
	    // to cast sm[1] to unsigned, to prune sign extenstion
	    ((uint32_t) htole16(sm[1]) << 16) |
	    ((uint64_t) htole16(sm[2]) << 32) ;
#endif
    // serialize size+mtime with base64
    static const char base64[] = "0123456789"
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	    "abcdefghijklmnopqrstuvwxyz" "+/";
    for (int i = 0; i < 8; i++, sm48 >>= 6)
	*key++ = base64[sm48 & 077];
    *key = '\0';
    return keylen;
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

Header hdrcache_get(const char *bn, const struct stat *st, unsigned *off)
{
    if (initialize() < 0)
	return NULL;
    char key[MAXKEY+1];
    int keylen = hdrcache_key(bn, st, key);
    if (keylen < 1)
	return NULL;
    struct cache_ent *ent;
    size_t entsize;
    if (!mcdb_get(env, key, keylen, (const void **) &ent, &entsize))
	return NULL;
    void *blob = ent->blob;
    char ublob[hdrsize_max];
    if (ent->compressed) {
	int blobsize = entsize - sizeof(struct cache_ent);
	lzo_uint ublobsize = 0;
	int rc = lzo1x_decompress(blob, blobsize, ublob, &ublobsize, NULL);
	if (rc != LZO_E_OK || ublobsize < 1 || ublobsize > hdrsize_max) {
	    fprintf(stderr, "%s %s: lzo1x_decompress failed\n", __func__, bn);
	    return NULL;
	}
	blob = ublob;
    }
    Header h = headerCopyLoad(blob);
    if (h == NULL) {
	fprintf(stderr, "%s %s: headerLoad failed\n", __func__, bn);
	return NULL;
    }
    if (off)
	*off = ent->off;
    free(ent);
    return h;
}

void hdrcache_put(const char *bn, const struct stat *st, Header h, unsigned off)
{
    if (initialize() < 0)
	return;
    char key[MAXKEY+1];
    int keylen = hdrcache_key(bn, st, key);
    if (keylen < 1)
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
	fprintf(stderr, "%s %s: headerLoad failed\n", __func__, bn);
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
    mcdb_put(env, key, keylen, ent, entsize);
}
