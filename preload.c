#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <rpm/rpmio.h>
#include <rpm/rpmlib.h>

static inline
bool endswith(const char *str, const char *suffix)
{
   size_t len1 = strlen(str);
   size_t len2 = strlen(suffix);
   if (len1 < len2)
      return false;
   str += (len1 - len2);
   if (strcmp(str, suffix))
      return false;
   return true;
}

// The cache works in two stages: first, it traces each Fopen() call and
// remembers the argument.  Then, if rpmReadPackageFile(fd) call follows,
// it checks to see if the fd is referencing the last open file.
struct last {
    char *path;
    struct stat st;
    FD_t fd;
};

static __thread
struct last thr_last;

__attribute__((visibility("default"),externally_visible))
FD_t Fopen(const char *path, const char *fmode)
{
    // pthread_getspecific(3) called only once
    struct last *last = &thr_last;
    if (last->path) {
	free(last->path);
	last->fd = NULL;
    }
    static // no need for thread var or write barrier
    FD_t (*next)(const char *path, const char *fmode);
    if (next == NULL) {
	next = dlsym(RTLD_NEXT, __func__);
	assert(next);
    }
    FD_t fd = next(path, fmode);
    if (fd && endswith(path, ".rpm") && *fmode == 'r' &&
	stat(path, &last->st) == 0 && S_ISREG(last->st.st_mode))
    {
	last->path = strdup(path);
	if (last->path)
	    last->fd = fd;
    }
    return fd;
}

#include "hdrcache.h"

__attribute__((visibility("default"),externally_visible))
rpmRC rpmReadPackageFile(rpmts ts, FD_t fd, const char *fn, Header *hdrp)
{
    Header hdr_;
    if (hdrp == NULL)
	hdrp = &hdr_;
    struct stat st;
    struct last *last = &thr_last;
    bool match =
	fd && fd == last->fd &&
	stat(last->path, &st) == 0 && S_ISREG(st.st_mode) &&
	st.st_dev == last->st.st_dev && st.st_ino == last->st.st_ino &&
	st.st_size == last->st.st_size && st.st_mtime == last->st.st_mtime;
    if (match) {
	unsigned off;
	*hdrp = hdrcache_get(last->path, &st, &off);
	if (*hdrp) {
	    int pos = lseek(Fileno(fd), off, SEEK_SET);
	    if (pos != off)
		*hdrp = headerFree(*hdrp);
	    else
		return RPMRC_OK;
	}
    }
    static
    rpmRC (*next)(rpmts ts, FD_t fd, const char *fn, Header *hdrp);
    if (next == NULL) {
	next = dlsym(RTLD_NEXT, __func__);
	assert(next);
    }
    rpmRC rc = next(ts, fd, fn, hdrp);
    if (match) {
	if (rc == RPMRC_OK || rc == RPMRC_NOTTRUSTED || rc == RPMRC_NOKEY) {
	    int pos = lseek(Fileno(fd), 0, SEEK_CUR);
	    if (pos > 0)
		hdrcache_put(last->path, &st, *hdrp, pos);
	}
    }
    return rc;
}

// ex: set ts=8 sts=4 sw=4 noet:
