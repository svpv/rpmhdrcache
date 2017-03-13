#ifndef QA_RPMCACHE_H
#define QA_RPMCACHE_H

#ifdef __cplusplus
extern "C" {
#endif

struct rpmcache *rpmcache_open(const char *name);
void rpmcache_clean(struct rpmcache *rpmcache, int days);
void rpmcache_close(struct rpmcache *rpmcache);

#ifndef __cplusplus
#include <stdbool.h>
#endif

// memcached needs ASCII keys no longer than 250 characters
#define MAXRPMKEYLEN 250

struct rpmkey {
    size_t len;
    char str[MAXRPMKEYLEN+1];
};

// An rpm package is identified by its key, an ASCII string, which combines basename,
// file size, and mtime.  Something like this: x11perf-1.5.4-alt1.x86_64@nEUq6B7h
bool rpmcache_key(const char *fname, unsigned fsize, unsigned mtime, struct rpmkey *key);

/* Concerning empty values and trailing null bytes,
 * see the note in cache.h. */

bool rpmcache_get(struct rpmcache *rpmcache,
	const struct rpmkey *key,
	void **valp /* malloc'd */, int *valsizep);
void rpmcache_put(struct rpmcache *rpmcache,
	const struct rpmkey *key,
	const void *val, int valsize);

#ifdef __cplusplus
}
#endif

#endif
