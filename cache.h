#ifndef QA_CACHE_H
#define QA_CACHE_H

#ifdef __cplusplus
extern "C" {
#endif

struct cache *cache_open(const char *dir);
void cache_clean(struct cache *cache, int days);
void cache_close(struct cache *cache);

#ifndef __cplusplus
#include <stdbool.h>
#endif

/*
 * Note that it is possible to store empty values by specifying valsize = 0
 * argument to cache_put (val is ignored in this case).  When fetching empty
 * value, cache_get returns true with *valsizep set to 0 and *valp set to NULL.
 *
 * Otherwise, when fetching non-empty values, cache_get will implicitly add
 * trailing null byte past the end of the value.  This is useful when values
 * are strings: *valsizep will be set to strlen and *valp can be immediately
 * used as a null-terminated string.  Note that the null byte itself is not
 * stored in the cache.
 */

bool cache_get(struct cache *cache,
	const void *key, int keysize,
	void **valp /* malloc'd */, int *valsizep);
void cache_put(struct cache *cache,
	const void *key, int keysize,
	const void *val, int valsize);

#ifdef __cplusplus
}
#endif

#endif
