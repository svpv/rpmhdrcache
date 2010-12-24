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

static __thread
const char *last_path;

static __thread
FD_t last_fd;

static __thread
struct stat last_st;

__attribute__((visibility("default"),externally_visible))
FD_t Fopen(const char *path, const char *fmode)
{
    last_path = _free(last_path);
    last_fd = NULL;
    static __thread
    FD_t (*Fopen_next)(const char *path, const char *fmode);
    if (Fopen_next == NULL) {
	Fopen_next = dlsym(RTLD_NEXT, __func__);
	assert(Fopen_next);
    }
    FD_t fd = Fopen_next(path, fmode);
    if (fd && endswith(path, ".rpm") && *fmode == 'r' &&
	stat(path, &last_st) == 0 && S_ISREG(last_st.st_mode))
    {
	last_path = strdup(path);
	if (last_path)
	    last_fd = fd;
    }
    return fd;
}

#include "hdrcache.h"

__attribute__((visibility("default"),externally_visible))
rpmRC rpmReadPackageHeader(FD_t fd, Header *hdrp,
	int *isSource, int *major, int *minor)
{
    Header hdr_;
    int isSource_, major_, minor_;
    if (hdrp == NULL)
	hdrp = &hdr_;
    if (isSource == NULL)
	isSource = &isSource_;
    if (major == NULL)
	major = &major_;
    if (minor == NULL)
	minor = &minor_;
    static __thread
    rpmRC (*rpmReadPackageHeader_next)(FD_t fd, Header *hdrp,
	    int *isSource, int *major, int *minor);
    if (rpmReadPackageHeader_next == NULL) {
	rpmReadPackageHeader_next = dlsym(RTLD_NEXT, __func__);
	assert(rpmReadPackageHeader_next);
    }
    struct stat st;
    bool match =
	fd && fd == last_fd &&
	stat(last_path, &st) == 0 && S_ISREG(st.st_mode) &&
	st.st_dev == last_st.st_dev && st.st_ino == last_st.st_ino &&
	st.st_size == last_st.st_size && st.st_mtime == last_st.st_mtime;
    if (match) {
	unsigned off;
	*hdrp = hdrcache_get(last_path, st, &off);
	if (*hdrp) {
	    int pos = lseek(Fileno(fd), off, SEEK_SET);
	    if (pos != off)
		*hdrp = headerFree(*hdrp);
	    else {
		*isSource = !headerIsEntry(*hdrp, RPMTAG_SOURCERPM);
		*major = 3;
		*minor = 0;
		return RPMRC_OK;
	    }
	}
    }
    rpmRC rc = rpmReadPackageHeader_next(fd, hdrp, isSource, major, minor);
    if (match) {
	if (rc == RPMRC_OK && *major == 3 && *minor == 0) {
	    int realSource = !headerIsEntry(*hdrp, RPMTAG_SOURCERPM);
	    if (realSource == *isSource) {
		int pos = lseek(Fileno(fd), 0, SEEK_CUR);
		if (pos > 0)
		    hdrcache_put(last_path, st, *hdrp, pos);
	    }
	}
    }
    return rc;
}

// ex: set ts=8 sts=4 sw=4 noet:
