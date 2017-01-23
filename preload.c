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
    // caching only works if we have rpm filename
    const char *fname = Fdescr(fd);
    // Fdescr(fd) can return "[none]" or "[fd %d]"
    if (fname == NULL || *fname == '[')
	fname = fn;
    // caching only works if we can stat the fd
    struct stat st;
    if (fname) {
	int fdno = Fileno(fd);
	if (fdno < 0 || fstat(fdno, &st) || !S_ISREG(st.st_mode))
	    fname = NULL;
    }
    // make the key
    struct key key;
    if (fname) {
	if (!hdrcache_key(fname, &st, &key))
	    fname = NULL;
    }
    // the caller may pass hdrp=NULL, but we want to fetch the header anyway
    Header hdr_;
    if (hdrp == NULL)
	hdrp = &hdr_;
    // get from the cache
    if (fname) {
	unsigned off;
	*hdrp = hdrcache_get(&key, &off);
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
    if (fname) {
	if (rc == RPMRC_OK || rc == RPMRC_NOTTRUSTED || rc == RPMRC_NOKEY) {
	    int pos = lseek(Fileno(fd), 0, SEEK_CUR);
	    if (pos > 0)
		hdrcache_put(&key, *hdrp, pos);
	}
    }
    return rc;
}
