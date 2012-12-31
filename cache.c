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
    if (!db_open(cache, dir)) {
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
    db_close(cache);
    close(cache->dirfd);
    free(cache);
}

#include <openssl/sha.h>

// Fast compression and decompression from Google.
#include <snappy-c.h>

bool cache_get(struct cache *cache,
	const void *key, int keysize,
	void **valp, int *valsizep)
{
    if (valp)
	*valp = NULL;
    if (valsizep)
	*valsizep = 0;

    SHA1(key, keysize, cache->sha1);

    if (!db_get(cache))
	if (!fs_get(cache))
	    return false;

    // validate
    if (cache->ventsize < sizeof(*cache->vent)) {
	ERROR("vent too small");
    unget:
	if (cache->vent != (void *) cache->vbuf)
	    fs_unget(cache);
	return false;
    }

    // update db atime
    if (cache->vent == (void *) cache->vbuf && cache->vent->atime < cache->now)
	db_atime(cache);

    // prepare for return
    if (cache->vent->flags & V_SNAPPY) {
	// uncompress
	int csize = cache->ventsize - sizeof(*cache->vent);
	size_t usize;
	if (snappy_uncompressed_length(cache->vent + 1, csize, &usize)) {
	    ERROR("snappy_uncompressed_length: invalid data");
	    goto unget;
	}
	if (valp) {
	    if ((*valp = malloc(usize + 1)) == NULL) {
		ERROR("malloc: %m");
		goto unget;
	    }
	    if (snappy_uncompress(cache->vent + 1, csize, *valp, &usize)) {
		ERROR("snappy_uncompress: invalid data");
		free(*valp);
		*valp = NULL;
		goto unget;
	    }
	    ((char *) *valp)[usize] = '\0';
	}
	else {
	    if (snappy_validate_compressed_buffer(cache->vent + 1, csize)) {
		ERROR("snappy_validate_compressed_buffer: invalid data");
		goto unget;
	    }
	}
	if (valsizep)
	    *valsizep = usize;
    }
    else {
	int size = cache->ventsize - sizeof(*cache->vent);
	if (size && valp) {
	    if ((*valp = malloc(size + 1)) == NULL) {
		ERROR("malloc: %m");
		goto unget;
	    }
	    memcpy(*valp, cache->vent + 1, size);
	    ((char *) *valp)[size] = '\0';
	}
	if (valsizep)
	    *valsizep = size;
    }

    if (cache->vent != (void *) cache->vbuf)
	fs_unget(cache);

    return true;
}

void cache_put(struct cache *cache,
	const void *key, int keysize,
	const void *val, int valsize)
{
    SHA1(key, keysize, cache->sha1);

    int max_csize = snappy_max_compressed_length(valsize);
    cache->vent = malloc(sizeof(*cache->vent) + max_csize);
    if (cache->vent == NULL) {
	ERROR("malloc: %m");
	return;
    }
    cache->vent->flags = 0;
    cache->vent->atime = 0;
    cache->vent->mtime = 0;
    cache->vent->pad = 0;
    size_t csize = max_csize;
    if (snappy_compress(val, valsize, cache->vent + 1, &csize)) {
	ERROR("snappy_compress: error");
    uncompressed:
	memcpy(cache->vent + 1, val, valsize);
	cache->ventsize = sizeof(*cache->vent) + valsize;
    }
    else if (csize >= valsize)
	goto uncompressed;
    else {
	cache->vent->flags |= V_SNAPPY;
	cache->ventsize = sizeof(*cache->vent) + csize;
    }

    if (cache->ventsize - sizeof(*cache->vent) <= MAX_DB_VAL_SIZE)
	db_put(cache);
    else {
	db_del(cache);
	fs_put(cache);
    }
}

void cache_clean(struct cache *cache, int days)
{
    if (days < 1) {
	ERROR("days must be greater than 0, got %d", days);
	return;
    }
    db_clean(cache, days);
    fs_clean(cache, days);
}

// ex:ts=8 sts=4 sw=4 noet
