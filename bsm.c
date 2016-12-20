#include <string.h>
#include <limits.h>
#include "cache.h"
#include "error.h"

static int bsm_key(char *key,
	const char *fname, const char *ext,
	unsigned fsize, unsigned mtime)
{
    const char *bn = strrchr(fname, '/');
    bn = bn ? bn + 1 : fname;
    size_t len = strlen(bn);
    if (len < 1 || len > NAME_MAX) {
	ERROR("invalid filename: %s", fname);
	return -1;
    }
    if (ext && *ext) {
	size_t elen = strlen(ext);
	if (elen < len && memcmp(bn + len - elen, ext, elen) == 0)
	    len -= elen;
	else
	    ERROR("filename doesn't end with %s: %s", ext, fname);
    }
    memcpy(key, bn, len);
    key[len] = '\0';
    unsigned sm[2] = { fsize, mtime };
    memcpy(key + len + 1, sm, sizeof sm);
    return len + 1 + sizeof sm;
}

bool bsm_get(struct cache *cache,
	const char *fname, const char *ext,
	unsigned fsize, unsigned mtime,
	void **valp, int *valsizep)
{
    char key[NAME_MAX + 1 + 2 * sizeof(unsigned)];
    int keysize = bsm_key(key, fname, ext, fsize, mtime);
    if (keysize < 1)
	return false;
    return cache_get(cache, key, keysize, valp, valsizep);
}

void bsm_put(struct cache *cache,
	const char *fname, const char *ext,
	unsigned fsize, unsigned mtime,
	const void *val, int valsize)
{
    char key[NAME_MAX + 1 + 2 * sizeof(unsigned)];
    int keysize = bsm_key(key, fname, ext, fsize, mtime);
    if (keysize < 1)
	return;
    cache_put(cache, key, keysize, val, valsize);
}
