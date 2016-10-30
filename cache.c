#include "cache.h"
#include "cache-impl.h"

struct cache *cache_open(const char *dir)
{
    // allocate cache
    struct cache *cache = malloc(sizeof(*cache));
    if (cache == NULL) {
	ERROR("malloc: %m");
	return NULL;
    }

    // open dir
    cache->dirfd = open(dir, O_RDONLY | O_DIRECTORY);
    if (cache->dirfd < 0) {
	// probably ENOENT
	ERROR("%s: %m", dir);
	free(cache);
	return NULL;
    }

    // initialize cache umask
    struct stat st;
    int rc = fstat(cache->dirfd, &st);
    if (rc < 0) {
	ERROR("fstat: %m");
	st.st_mode = 0755;
    }
    cache->umask = (~st.st_mode & 022);

    // initialize timestamp
    cache->now = time(NULL) / 3600 / 24;

    // initialize db backend
    if (!qadb_open(cache, dir)) {
	close(cache->dirfd);
	free(cache);
	return NULL;
    }

    return cache;
}

void cache_close(struct cache *cache)
{
    if (cache == NULL)
	return;
    qadb_close(cache);
    close(cache->dirfd);
    free(cache);
}

#include <openssl/sha.h>

// Fast compression and decompression from Facebook.
#include <zstd.h>

// When compressing a sequence of 17 repeated characters,
// zstd's output size is 17.  So it is pointless to even
// try to compress anything below this size:
#define MIN_COMPRESS_SIZE 18

bool cache_get(struct cache *cache,
	const void *key, int keysize,
	void **valp, int *valsizep)
{
    if (valp)
	*valp = NULL;
    if (valsizep)
	*valsizep = 0;

    char vbuf[sizeof(struct cache_ent) + MAX_DB_VAL_SIZE] __attribute__((aligned(4)));
    struct cache_ent *vent = (void *) vbuf;
    int ventsize = sizeof(vbuf);

    if (!qadb_get(cache, key, keysize, vent, &ventsize)) {
	unsigned char sha1[20] __attribute__((aligned(4)));
	SHA1(key, keysize, sha1);
	if (!qafs_get(cache, sha1, (void **) &vent, &ventsize))
	    return false;
    }

    // validate
    if (ventsize < sizeof(*vent)) {
	ERROR("vent too small");
    unget:
	if (vent != (void *) vbuf)
	    qafs_unget(vent, ventsize);
	return false;
    }

    // prepare for return
    if (vent->flags & V_SNAPPY) {
	// We used to have snappy, but zstd provides a much better compromise
	// for big data sets which we have; so, force a miss.
	goto unget;
    }
    else if (vent->flags & V_ZSTD) {
	// uncompress
	int csize = ventsize - sizeof(*vent);
	if (csize < 1) {
	    ERROR("compressed vent too small");
	    goto unget;
	}
	size_t usize = ZSTD_getDecompressedSize(vent + 1, csize);
	if (usize < MIN_COMPRESS_SIZE || usize > INT_MAX) {
	    ERROR("ZSTD_getDecompressedSize: invalid data");
	    goto unget;
	}
	if (valp) {
	    if ((*valp = malloc(usize + 1)) == NULL) {
		ERROR("malloc: %m");
		goto unget;
	    }
	    usize = ZSTD_decompress(*valp, usize, vent + 1, csize);
	    if (usize < MIN_COMPRESS_SIZE || usize > INT_MAX) {
		ERROR("ZSTD_decompress: invalid data");
		free(*valp);
		*valp = NULL;
		goto unget;
	    }
	    ((char *) *valp)[usize] = '\0';
	}
	if (valsizep)
	    *valsizep = usize;
    }
    else {
	int size = ventsize - sizeof(*vent);
	if (size && valp) {
	    if ((*valp = malloc(size + 1)) == NULL) {
		ERROR("malloc: %m");
		goto unget;
	    }
	    memcpy(*valp, vent + 1, size);
	    ((char *) *valp)[size] = '\0';
	}
	if (valsizep)
	    *valsizep = size;
    }

    if (vent != (void *) vbuf)
	qafs_unget(vent, ventsize);

    return true;
}

void cache_put(struct cache *cache,
	const void *key, int keysize,
	const void *val, int valsize)
{
    int max_valsize;
    if (valsize < MIN_COMPRESS_SIZE)
	max_valsize = valsize;
    else {
	max_valsize = ZSTD_compressBound(valsize);
	if (max_valsize < valsize) {
	    ERROR("ZSTD_compressBound: error");
	    return;
	}
    }

    struct cache_ent *vent = malloc(sizeof(*vent) + max_valsize);
    if (vent == NULL) {
	ERROR("malloc: %m");
	return;
    }
    vent->flags = 0;
    vent->atime = 0;
    vent->mtime = 0;
    vent->pad = 0;

    int ventsize;
    if (valsize < MIN_COMPRESS_SIZE) {
    uncompressed:
	memcpy(vent + 1, val, valsize);
	ventsize = sizeof(*vent) + valsize;
    }
    else {
	size_t csize = ZSTD_compress(vent + 1, max_valsize, val, valsize, 3);
	if (csize < 1 || csize > INT_MAX) {
	    ERROR("ZSTD_compress: error");
	    free(vent);
	    return;
	}
	if (csize >= valsize)
	    goto uncompressed;
	vent->flags |= V_ZSTD;
	ventsize = sizeof(*vent) + csize;
    }

    if (ventsize - sizeof(*vent) <= MAX_DB_VAL_SIZE)
	qadb_put(cache, key, keysize, vent, ventsize);
    else {
	qadb_del(cache, key, keysize);
	unsigned char sha1[20] __attribute__((aligned(4)));
	SHA1(key, keysize, sha1);
	qafs_put(cache, sha1, vent, ventsize);
    }

    free(vent);
}

void cache_clean(struct cache *cache, int days)
{
    if (days < 1) {
	ERROR("days must be greater than 0, got %d", days);
	return;
    }
    qadb_clean(cache, days);
    qafs_clean(cache, days);
}

// ex:ts=8 sts=4 sw=4 noet
