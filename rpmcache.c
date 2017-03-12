#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/stat.h> // mkdir
#include "cache.h"
#include "rpmcache.h"
#include "error.h"
#include "cache.h"

// prepend toplevel dir, i.e. make ~/.rpmcache/dir string
static char *catdir(const char *dir, size_t *plen)
{
    const char *home = getenv("HOME");
    if (!(home && *home)) {
	ERROR("HOME not set");
	return NULL;
    }
    static const char homesubdir[] = "/.rpmcache/";
    size_t len1 = strlen(home);
    size_t len2 = strlen(dir);
    *plen = len1 + len2 + sizeof(homesubdir) - 1;
    char *fulldir = malloc(*plen + 1);
    if (fulldir == NULL) {
	ERROR("malloc: %m");
	return NULL;
    }
    memcpy(fulldir, home, len1);
    memcpy(fulldir + len1, homesubdir, sizeof(homesubdir) - 1);
    memcpy(fulldir + len1 + sizeof(homesubdir) - 1, dir, len2 + 1);
    return fulldir;
}

struct rpmcache *rpmcache_open(const char *dir)
{
    // make full directory path
    char *fulldir;
    size_t fulldlen;
    if (*dir == '/') {
	fulldir = strdup(dir);
	if (fulldir == NULL) {
	    ERROR("strdup: %m");
	    return NULL;
	}
	fulldlen = strlen(fulldir);
    }
    else {
	fulldir = catdir(dir, &fulldlen);
	if (fulldir == NULL)
	    return NULL;
    }

    // mkdir fulldir
    if (*dir != '/') {
	// create ~/.rpmcache dir
	char *slash = strrchr(fulldir, '/');
	assert(slash);
	*slash = '\0';
	int rc = mkdir(fulldir, 0777);
	if (rc == 0)
	    ERROR("created toplevel directory %s", fulldir);
	else if (errno != EEXIST) {
	    ERROR("mkdir: %s: %m", fulldir);
	    free(fulldir);
	    return NULL;
	}
	*slash = '/';
    }
    // create the dir
    int rc = mkdir(fulldir, 0777);
    if (rc == 0)
	ERROR("created directory %s", fulldir);
    else if (errno != EEXIST) {
	ERROR("mkdir: %s: %m", fulldir);
	free(fulldir);
	return NULL;
    }

    // open the cache
    return (struct rpmcache *) cache_open(fulldir);
}

bool rpmcache_get(struct rpmcache *rpmcache,
	const struct rpmkey *key,
	void **valp, int *valsizep)
{
    return
    cache_get((struct cache *) rpmcache,
	    key->str, key->len,
	    valp, valsizep);
}

void rpmcache_put(struct rpmcache *rpmcache,
	const struct rpmkey *key,
	const void *val, int valsize)
{
    cache_get((struct cache *) rpmcache,
	    key->str, key->len,
	    val, valsize);
}

void rpmcache_close(struct rpmcache *rpmcache)
{
    cache_close((struct cache *) rpmcache);
}

// ex:ts=8 sts=4 sw=4 noet
