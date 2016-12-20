#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/stat.h> // mkdir
#include "cache.h"
#include "rpmcache.h"
#include "rpmarch.h"
#include "error.h"

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

/*
 * A key is something that (fname,fsize,mtime) triple is digested into.
 * Namely, it is digested into two parts: 1) an architecture found in
 * rpm basename; this must be a valid rpm architecture; it will be used
 * as a subdir/ under fulldir/ to open a separate arch-specific cache;
 * 2) a key for lookup in that cache, consisting of (name,fsize,mtime)
 * where name is rpm basename devoid of .arch.rpm suffix.
 */
#include <limits.h> // need NAME_MAX

// NAME_MAX is typically 255 and does not count a terminating null byte.
// To improve collation, we do place a null byte between name and (size,mtime).
#define KEY_DATA_MAX (NAME_MAX	- (sizeof(".rpm") - 1) \
				- (ARCH_MIN_WORD_LENGTH + /* dot */ 1) \
				+ 1 + 2 * sizeof(unsigned))

// also need something like NAME_MIN for N-V-R.A.rpm
#define RPM_NAME_MIN (2 + 2 + 2 + ARCH_MIN_WORD_LENGTH + 4)

struct key {
    const char *arch;
    int datasize;
    char data[KEY_DATA_MAX];
};

// to make collation by mtime effective, we need to convert
// mtime to big endian aka network byte order
#include <arpa/inet.h>

static bool make_key(struct key *k,
	const char *fname, unsigned fsize, unsigned mtime)
{
    // validate basename
    const char *slash = strrchr(fname, '/');
    if (slash)
	fname = slash + 1;
    size_t len = strlen(fname);
    if (len < RPM_NAME_MIN || len > NAME_MAX) {
	ERROR("invalid rpm filename: %s", fname);
	return false;
    }
    const char *dotrpm = fname + len - 4;
    if (memcmp(dotrpm, ".rpm", 4)) {
	ERROR("missing .rpm suffix: %s", fname);
	return false;
    }

    // validate arch
    len -= 4;
    memcpy(k->data, fname, len);
    k->data[len] = '\0';
    char *arch = strrchr(k->data, '.');
    if (arch)
	*arch++ = '\0';
    else {
	ERROR("cannot find .arch.rpm suffix: %s", fname);
	return false;
    }
    unsigned alen = len - (arch - k->data);
    k->arch = validate_rpm_arch(arch, alen);
    if (!k->arch) {
	ERROR("invalid .arch.rpm suffix: %s", fname);
	return false;
    }

    // append (size,mtime)
    len -= alen + 1;
    fsize = htonl(fsize);
    mtime = htonl(mtime);
    // collate by mtime first
    memcpy(k->data + len + 1, &mtime, sizeof mtime);
    memcpy(k->data + len + 1 + sizeof mtime, &fsize, sizeof fsize);
    k->datasize = len + 1 + sizeof fsize + sizeof mtime;
    return true;
}

bool rpmcache_get(struct rpmcache *rpmcache,
	const char *fname, unsigned fsize, unsigned mtime,
	void **valp, int *valsizep)
{
    struct key k;
    if (!make_key(&k, fname, fsize, mtime))
	return false;
    return false;
}

void rpmcache_put(struct rpmcache *rpmcache,
	const char *fname, unsigned fsize, unsigned mtime,
	const void *val, int valsize)
{
    struct key k;
    if (!make_key(&k, fname, fsize, mtime))
	return;
}

// ex:ts=8 sts=4 sw=4 noet
