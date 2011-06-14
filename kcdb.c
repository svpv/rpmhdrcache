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

#include <kclangc.h>
#include "kcdb.h"

struct kcdbenv {
    int dirfd;
    unsigned umask;
    struct kcdbdb *kcdblist;
    char dirname[1];
};

struct kcdbdb {
    KCDB *db;
    struct kcdbdb *next;
    unsigned hash;
    char dbname[1];
};

#define ERROR(fmt, args...) \
    fprintf(stderr, "%s: %s: " fmt "\n", \
	    program_invocation_short_name, __func__, ##args)

struct kcdbenv *kcdbenv_open(const char *dirname)
{
    int rc;

    // 1: allocate env
    struct kcdbenv *env4 = malloc(sizeof(*env4) + strlen(dirname));
    if (env4 == NULL) {
	ERROR("malloc: %m");
	return NULL;
    }
    strcpy(env4->dirname, dirname);
    env4->kcdblist = NULL;

    env4->dirfd = open(env4->dirname, O_RDONLY | O_DIRECTORY);
    if (env4->dirfd < 0) {
	ERROR("open %s: %m", env4->dirname);
	free(env4);
	return NULL;
    }

    // initialize umask
    struct stat st;
    rc = fstat(env4->dirfd, &st);
    if (rc < 0) {
	ERROR("fstat %s: %m", env4->dirname);
	close(env4->dirfd);
	free(env4);
	return NULL;
    }
    env4->umask = (~st.st_mode & 022);

    return env4;
}

void kcdbenv_close(struct kcdbenv *env4)
{
    if (env4 == NULL)
	return;

    int rc;

    // close databases
    while (env4->kcdblist) {
	struct kcdbdb *kcdb = env4->kcdblist;
	env4->kcdblist = kcdb->next;
	rc = kcdbclose(kcdb->db);
	if (!rc)
	    ERROR("db_close %s: %s", kcdb->dbname, kcdbemsg(kcdb->db));
	kcdbdel(kcdb->db);
	
    }
    close(env4->dirfd);
    free(env4);
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

struct kcdbdb *kcdbenv_db(struct kcdbenv *env4, const char *dbname)
{
    // see if db is already open
    int rc;
    unsigned hash = strhash(dbname);
    struct kcdbdb *kcdb = env4->kcdblist, *prev = NULL;
    while (kcdb) {
	if (hash == kcdb->hash && strcmp(dbname, kcdb->dbname) == 0) {
	    // found, move to front
	    if (kcdb != env4->kcdblist) {
		prev->next = kcdb->next;
		kcdb->next = env4->kcdblist;
		env4->kcdblist = kcdb;
	    }
	    return kcdb;
	}
	prev = kcdb;
	kcdb = kcdb->next;
    }

    // 1: allocate kcdb
    int len = strlen(dbname);
    kcdb = malloc(sizeof(*kcdb) + strlen(env4->dirname) + len + sizeof(".kch"));
    if (kcdb == NULL) {
	ERROR("malloc: %m");
	return NULL;
    }

    // 2: allocate db
    kcdb->db=kcdbnew();

    // prepare filename
    kcdb->hash = hash;
    strcpy(kcdb->dbname, env4->dirname);
    strcpy(kcdb->dbname + strlen(env4->dirname), "/");
    strcpy(kcdb->dbname + strlen(env4->dirname) + 1, dbname);
    strcpy(kcdb->dbname + + strlen(env4->dirname) + 1 + len, ".kch");

    // lock dir
    do
	rc = flock(env4->dirfd, LOCK_EX);
    while (rc < 0 && errno == EINTR);
    if (rc) {
	ERROR("LOCK_EX %s: %m", env4->dirname);
	// 2
	kcdbclose(kcdb->db);
	kcdbdel(kcdb->db);
	// 1
	free(kcdb);
	return NULL;
    }

    // block signals
    sigset_t set, oldset;
    sigfillset(&set);
    sigprocmask(SIG_BLOCK, &set, &oldset);

    // adjust umask
    unsigned omask = umask(env4->umask);

    // open db
    rc = kcdbopen(kcdb->db, (char *)kcdb->dbname, KCOWRITER | KCOCREATE);

    if (omask != env4->umask)
	umask(omask);
    sigprocmask(SIG_SETMASK, &oldset, NULL);
    flock(env4->dirfd, LOCK_UN);

    if (!rc) {
	ERROR("kcdbdopen: %s", kcdbemsg(kcdb->db));
	// 2
	kcdbclose(kcdb->db);
	kcdbdel(kcdb->db);
	// 1
	free(kcdb);
	return NULL;
    }

    // truncate ".kch" suffix
    kcdb->dbname[len] = '\0';

    // push to front
    kcdb->next = env4->kcdblist;
    env4->kcdblist = kcdb;

    return kcdb;
}

bool kcdbdb_get(struct kcdbdb *kcdb,
	const void *key, int keysize,
	const void **datap, int *datasizep)
{
    *datap = kcdbget(kcdb->db, key, keysize, datasizep);
    if (!*datap) {
	if (kcdbecode(kcdb->db)!=KCENOREC)
		ERROR("kcdbget %s: %s", kcdb->dbname, kcdbemsg(kcdb->db));
	return false;
    }

    return true;
}

bool kcdbdb_put(struct kcdbdb *kcdb,
	const void *key, int keysize,
	const void *data, int datasize)
{
    int rc = kcdbset(kcdb->db, key, keysize, data, datasize);

    if (!rc) {
	ERROR("kcdbset %s: %s", kcdb->dbname, kcdbemsg(kcdb->db));
	return false;
    }
    return true;
}
