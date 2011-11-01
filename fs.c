#include "cache-impl.h"

// Convert 20-byte sha1 to "XX/YYY..." filename.
static
void sha1_filename(const unsigned char *sha1, char *fname, bool tmp)
{
    static const char hex[] = "0123456789abcdef";
    inline
    void sha1_byte()
    {
	*fname++ = hex[*sha1 & 0x0f];
	*fname++ = hex[*sha1++ >> 4];
    }
    sha1_byte();
    *fname++ = '/';
    int i;
    for (i = 1; i < 20; i++)
	sha1_byte();
    if (tmp) {
	*fname++ = '.';
	int rnd = rand();
	sha1 = (const unsigned char *) &rnd;
	for (i = 0; i < 4; i++)
	    sha1_byte();
    }
    *fname = '\0';
}

#include <dirent.h>
#include <sys/mman.h>

bool fs_get(struct cache *cache)
{
    char fname[42];
    sha1_filename(cache->sha1, fname, false);
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
    cache->ventsize = st.st_size;
    cache->vent = mmap(NULL, cache->ventsize, PROT_READ, MAP_SHARED, fd, 0);
    if (cache->vent == MAP_FAILED) {
	ERROR("mmap: %m");
	close(fd);
	return false;
    }
    close(fd);
    return true;
}

void fs_unget(struct cache *cache)
{
    int rc = munmap(cache->vent, cache->ventsize);
    if (rc < 0)
	ERROR("munmap: %m");
}

void fs_put(struct cache *cache)
{
    // open tmp file
    char fname[51];
    sha1_filename(cache->sha1, fname, true);
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
    rc = ftruncate(fd, cache->ventsize);
    if (rc < 0) {
	ERROR("ftruncate: %m");
	close(fd);
	return;
    }
    void *dest = mmap(NULL, cache->ventsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (dest == MAP_FAILED) {
	ERROR("mmap: %m");
	close(fd);
	return;
    }
    close(fd);

    // write data
    memcpy(dest, cache->vent, cache->ventsize);
    rc = munmap(dest, cache->ventsize);
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

void fs_clean(struct cache *cache, int days)
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
