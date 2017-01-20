#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <libmemcached-1.0/memcached.h>
#include "mcdb.h"

struct mcdb *mcdb_open(const char *configstring)
{
    assert(configstring && *configstring);
    memcached_st *memc = memcached(configstring, strlen(configstring));
    if (!memc)
	fprintf(stderr, "%s: %s\n", program_invocation_short_name, "cannot initialize memcached");
    return (void *) memc;
}

void mcdb_close(struct mcdb *db)
{
    memcached_free((void *) db);
}

bool mcdb_get(struct mcdb *db,
	const char *key, size_t keylen,
	const void **datap, size_t *datasizep)
{
    memcached_st *memc = (void *) db;
    memcached_return_t rc;
    uint32_t flags;
    assert(datap && datasizep);
    *datap = memcached_get(memc, key, keylen, datasizep, &flags, &rc);
    if (*datap == NULL) {
        if (rc != MEMCACHED_NOTFOUND)
	    fprintf(stderr, "%s: %s: %s\n", "memcached_get", key, memcached_strerror(memc, rc));
	return false;
    }
    return true;
}

void mcdb_put(struct mcdb *db,
	const char *key, size_t keylen,
	const void *data, size_t datasize)
{
    memcached_st *memc = (void *) db;
    memcached_return_t rc = memcached_set(memc, key, keylen, data, datasize, 0, 0);
    if (rc != MEMCACHED_SUCCESS)
	fprintf(stderr, "%s: %s: %s\n", "memcached_set", key, memcached_strerror(memc, rc));
}
