#ifndef QA_RPMCACHE_H
#define QA_RPMCACHE_H

#ifdef __cplusplus
extern "C" {
#endif

struct rpmcache *rpmcache_open(const char *dir);
void rpmcache_clean(struct rpmcache *rpmcache, int days);
void rpmcache_close(struct rpmcache *rpmcache);

#ifndef __cplusplus
#include <stdbool.h>
#endif

/* Concerning empty values and trailing null bytes,
 * see the note in cache.h. */

bool rpmcache_get(struct rpmcache *rpmcache,
	const char *fname, unsigned fsize, unsigned mtime,
	void **valp /* malloc'd */, int *valsizep);
void rpmcache_put(struct rpmcache *rpmcache,
	const char *fname, unsigned fsize, unsigned mtime,
	const void *val, int valsize);

#ifdef __cplusplus
}
#endif

#endif
