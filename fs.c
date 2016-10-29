#include "cache-impl.h"

// Convert 20-byte sha1 to "XX/YYY..." filename.
static
void sha1_filename(const unsigned char *sha1, char *fname, int pid)
{
    static const char hex[] = "0123456789abcdef";
#define SHA1_BYTE { \
	*fname++ = hex[*sha1 & 0x0f]; \
	*fname++ = hex[*sha1++ >> 4]; \
    }
    SHA1_BYTE;
    *fname++ = '/';
    for (int i = 1; i < 20; i++)
	SHA1_BYTE;
    if (pid) {
	*fname++ = '.';
	unsigned tmp = rand();
	tmp = (tmp << 1) ^ pid;
	sha1 = (const unsigned char *) &tmp;
	for (int i = 0; i < 4; i++)
	    SHA1_BYTE;
    }
#undef SHA1_BYTE
    *fname = '\0';
}

#include <dirent.h>
#include <sys/mman.h>

bool qafs_get(struct cache *cache,
	const unsigned char *sha1,
	void **valp, int *valsizep)
{
    char fname[42];
    sha1_filename(sha1, fname, 0);
    int fd = openat(cache->dirfd, fname, O_RDONLY);
    if (fd < 0) {
	if (errno != ENOENT)
	    ERROR("openat: %m");
	return false;
    }
    struct stat st;
    int rc = fstat(fd, &st);
    if (rc < 0) {
	ERROR("fstat: %m");
	return false;
    }
    int valsize = st.st_size;
    void *val = mmap(NULL, valsize, PROT_READ, MAP_SHARED | MAP_POPULATE, fd, 0);
    if (val == MAP_FAILED) {
	ERROR("mmap: %m");
	close(fd);
	return false;
    }
    close(fd);
    if (valp)
	*valp = val;
    if (valsizep)
	*valsizep = valsize;
    return true;
}

void qafs_unget(void *val, int valsize)
{
    int rc = munmap(val, valsize);
    if (rc < 0)
	ERROR("munmap: %m");
}

void qafs_put(struct cache *cache,
	const unsigned char *sha1,
	const void *val, int valsize)
{
    // open tmp file
    char fname[51];
    sha1_filename(sha1, fname, cache->pid);
    fname[2] = '\0';
    SET_UMASK(cache);
    int rc = mkdirat(cache->dirfd, fname, 0777);
    if (rc < 0 && errno != EEXIST)
	ERROR("mkdirat: %m");
    fname[2] = '/';
    int fd = openat(cache->dirfd, fname, O_RDWR | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
	ERROR("openat: %m");
	UNSET_UMASK(cache);
	return;
    }
    UNSET_UMASK(cache);

    // extend and mmap for write
    rc = ftruncate(fd, valsize);
    if (rc < 0) {
	ERROR("ftruncate: %m");
	close(fd);
	return;
    }
    void *dest = mmap(NULL, valsize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
    if (dest == MAP_FAILED) {
	ERROR("mmap: %m");
	close(fd);
	return;
    }
    close(fd);

    // write data
    memcpy(dest, val, valsize);
    rc = munmap(dest, valsize);
    if (rc < 0)
	ERROR("munmap: %m");

    // move to permanent location
    char outfname[42];
    memcpy(outfname, fname, 41);
    outfname[41] = '\0';
    rc = renameat(cache->dirfd, fname, cache->dirfd, outfname);
    if (rc < 0)
	ERROR("renameat: %m");
}

void qafs_clean(struct cache *cache, int days)
{
    static const char hex[] = "0123456789abcdef";
    const char *a1, *a2;
    for (a1 = hex; *a1; a1++)
    for (a2 = hex; *a2; a2++) {
	int rc;
	const char dir[] = { *a1, *a2, '\0' };
	int dirfd = openat(cache->dirfd, dir, O_RDONLY | O_DIRECTORY);
	if (dirfd < 0) {
	    if (errno != ENOENT)
		ERROR("openat: %m");
	    continue;
	}

	DIR *dirp = fdopendir(dirfd);
	if (dirp == NULL) {
	    ERROR("fdopendir: %m");
	    close(dirfd);
	    continue;
	}

	struct dirent *dent;
	while ((dent = readdir(dirp)) != NULL) {
	    int len = strlen(dent->d_name);
	    if (len < 38)
		continue;

	    struct stat st;
	    rc = fstatat(dirfd, dent->d_name, &st, 0);
	    if (rc) {
		ERROR("fstatat: %m");
		continue;
	    }

	    unsigned short mtime = st.st_mtime / 3600 / 24;
	    unsigned short atime = st.st_atime / 3600 / 24;
	    if (len == 38) {
		if (mtime + days >= cache->now) continue;
		if (atime + days >= cache->now) continue;
	    }
	    else {
		// stale temporary files?
		if (mtime + 1 >= cache->now) continue;
		if (atime + 1 >= cache->now) continue;
	    }

	    rc = unlinkat(dirfd, dent->d_name, 0);
	    if (rc)
		ERROR("unlinkat: %m");
	}

	rc = closedir(dirp);
	if (rc)
	    ERROR("closedir: %m");
    }
}
