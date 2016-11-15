#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/stat.h> // mkdir
#include "cache.h"
#include "rpmcache.h"
#include "rpmarch.h"

#define ERROR(fmt, args...) \
    fprintf(stderr, "%s: %s: " fmt "\n", \
	    program_invocation_short_name, __func__, ##args)

// cache for an architecture
struct acache {
    struct cache *cache;
    // points to gperf static memory
    const char *arch;
};

struct rpmcache {
    // the smallest possible LRU cache (for two architecures)
    struct acache ac[2];
    // in this dir, there will be arch subdirs
    char dir[];
};

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

    // allocate rpmcache
    struct rpmcache *c = malloc(sizeof(*c) + fulldlen + 1);
    if (c == NULL) {
	ERROR("malloc: %m");
	return NULL;
    }
    memset(c->ac, 0, sizeof c->ac);
    memcpy(c->dir, fulldir, fulldlen + 1);
    free(fulldir);
    return c;
}

// ex:ts=8 sts=4 sw=4 noet
