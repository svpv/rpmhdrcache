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
