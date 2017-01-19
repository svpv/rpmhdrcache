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
#include "hdrcache.h"

__attribute__((visibility("default"),externally_visible))
rpmRC rpmReadPackageFile(rpmts ts, FD_t fd, const char *fn, Header *hdrp)
{
    // caching only works if we have rpm basename
    const char *bn = Fdescr(fd);
    // Fdescr(fd) can return "[none]" or "[fd %d]"
    if (*bn == '[')
	bn = fn;
    if (bn) {
	const char *slash = strrchr(bn, '/');
	if (slash)
	    bn = slash + 1;
	// validate basename
	size_t len = strlen(bn);
	if (len < sizeof("a-1-1.src.rpm") - 1)
	    bn = NULL;
	else {
	    const char *dotrpm = bn + len - 4;
	    if (memcmp(dotrpm, ".rpm", 4))
		bn = NULL;
	}
    }
    // caching only works if we can stat the fd
    struct stat st;
    if (bn) {
	int fdno = Fileno(fd);
	if (fdno < 0 || fstat(fdno, &st) || !S_ISREG(st.st_mode))
	    bn = NULL;
    }
    // the caller may pass hdrp=NULL, but we want to fetch the header anyway
    Header hdr_;
    if (hdrp == NULL)
	hdrp = &hdr_;
    // get from the cache
    if (bn) {
	unsigned off;
	*hdrp = hdrcache_get(bn, &st, &off);
	if (*hdrp) {
	    int pos = lseek(Fileno(fd), off, SEEK_SET);
	    if (pos != off)
		*hdrp = headerFree(*hdrp);
	    else
		return RPMRC_OK;
	}
    }
    // call the real __func__
    static
    rpmRC (*next)(rpmts ts, FD_t fd, const char *fn, Header *hdrp);
    if (next == NULL) {
	next = dlsym(RTLD_NEXT, __func__);
	assert(next);
    }
    rpmRC rc = next(ts, fd, fn, hdrp);
    // put to the cache
    if (bn) {
	if (rc == RPMRC_OK || rc == RPMRC_NOTTRUSTED || rc == RPMRC_NOKEY) {
	    int pos = lseek(Fileno(fd), 0, SEEK_CUR);
	    if (pos > 0)
		hdrcache_put(bn, &st, *hdrp, pos);
	}
    }
    return rc;
}

// ex: set ts=8 sts=4 sw=4 noet:
