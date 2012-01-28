#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/file.h>

#include <libmemcachedutil-1.0/pool.h>

#include "mcdb.h"

struct mcdbenv {
    memcached_pool_st* pool;
    memcached_st *ptr;
    char configstring[1];
};

#define ERROR(fmt, args...) \
    fprintf(stderr, "%s: %s: " fmt "\n", \
	    program_invocation_short_name, __func__, ##args)

struct mcdbenv *mcdbenv_open(const char *configstring)
{
    int rc;

    // 1: allocate env
    struct mcdbenv *env = malloc(sizeof(*env) + strlen(configstring));
    if (env == NULL) {
	ERROR("malloc: %m");
	return NULL;
    }
    strcpy(env->configstring, configstring);

    if (*configstring=='/') {
	env->pool = NULL;
	env->ptr =  memcached_create(NULL);
	memcached_return_t rc = memcached_server_add_unix_socket(env->ptr, env->configstring);
	if (!env->ptr) {
		ERROR("can't create memcached ptr: %s!",configstring);
		free(env);
		return NULL;
	}
    }
    else
    {
	env->ptr = NULL;
        env->pool =  memcached_pool(env->configstring, strlen(env->configstring));
	if (!env->pool) {
		ERROR("can't create memcached poll: %s!",configstring);
		free(env);
		return NULL;
	}
    }


    return env;
}

void mcdbenv_close(struct mcdbenv *env)
{
    if (env == NULL)
	return;

    memcached_pool_destroy(env->pool);

    free(env);
}

static inline
unsigned strhash(const char *str)
{
    unsigned h = 5381;
    unsigned char c;
    while ((c = *str++))
	h = h * 33 + c;
    return h;
}

bool mcdb_get(struct mcdbenv *env,
	const void *key, int keysize,
	const void **datap, size_t *datasizep)
{
    bool rv = true;

    memcached_return_t rc;

    memcached_st *memc;
    if (env->pool) memc = memcached_pool_pop(env->pool, false, &rc);
    else memc = env->ptr;

    if (!memc) {
	ERROR("can't get memcached server from pool %i", rc);
    }

    uint32_t flags;

    *datap = memcached_get(memc, key, keysize,datasizep, &flags ,&rc);

    if (rc != MEMCACHED_SUCCESS) {
        if (rc != MEMCACHED_NOTFOUND) {
		ERROR("can't get data from memcached %i", rc);
	}
	rv = false;
    }

    if (env->pool) memcached_pool_push(env->pool, memc);

    return rv;
}

bool mcdb_put(struct mcdbenv *env,
	const void *key, int keysize,
	const void *data, size_t datasize)
{
    bool rv = true;

    memcached_return_t rc;

    memcached_st *memc;
    if (env->pool) memc = memcached_pool_pop(env->pool, false, &rc);
    else memc = env->ptr;

    if (!memc) {
	ERROR("can't get memcached server from pool %i", rc);
    }


    rc = memcached_set( memc, key, keysize , data, datasize, 
						(time_t)0,     // time to live, need to change this
                                                (uint32_t)0
                                        );
    if (rc != MEMCACHED_SUCCESS) {
	ERROR("can't set data to memcached %i", rc);
	rv = false;
    }

    if (env->pool) memcached_pool_push(env->pool, memc);

    return rv;
}
