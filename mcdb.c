#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <libmemcached-1.0/memcached.h>
#include "mcdb.h"

#define progname program_invocation_short_name

struct mcdb *mcdb_open(const char *configstring)
{
    assert(configstring && *configstring);
    memcached_st *memc = memcached(configstring, strlen(configstring));
    if (!memc) {
	char buf[1024];
	buf[0] = '\0';
	memcached_return_t rc = libmemcached_check_configuration(configstring, strlen(configstring), buf, sizeof buf);
	if (rc == MEMCACHED_SUCCESS && buf[0] == '\0')
	    fprintf(stderr, "%s: %s\n", progname, "cannot initialize memcached");
	else if (buf[0])
	    fprintf(stderr, "%s: %s: %.*s\n", progname, "memcached", (int) sizeof buf, buf);
	else
	    fprintf(stderr, "%s: %s: %s\n", progname, "memcached", memcached_strerror(NULL, rc));
    }
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

static memcached_return_t stat_cb(const memcached_instance_st *server,
	const char *key, size_t klen,
	const char *val, size_t vlen, void *arg)
{
    (void) server;
    int *psize = arg;
    // the fact that we're called means that at least one server is alive
    if (*psize < 0)
	*psize = 0;
#define KEY "item_size_max"
#define KLEN (sizeof KEY - 1)
    if (klen != KLEN || memcmp(key, KEY, KLEN))
	return MEMCACHED_SUCCESS;
    (void) vlen;
    int size = atoi(val);
    // take the minimum size among the servers
    if (size > 0 && (*psize == 0 || size < *psize))
	*psize = size;
    return MEMCACHED_SUCCESS;
}

int mcdb_max_item_size(struct mcdb *db)
{
    memcached_st *memc = (void *) db;
    int size = -1;
    memcached_return_t rc = memcached_stat_execute(memc, "settings", stat_cb, &size);
    // fall back to the default size
    if (size == 0)
	size = 1 << 20;
    if (size > 0)
	return size;
    if (rc != MEMCACHED_SUCCESS)
	fprintf(stderr, "%s: %s: %s\n", progname, "memcached_stat_execute", memcached_strerror(NULL, rc));
    else
	fprintf(stderr, "%s: %s\n", progname, "cannot connect to memcached");
    return -1;
}
