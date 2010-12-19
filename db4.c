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

#include <db.h>
#include "db4.h"

#if DB_VERSION_MAJOR < 4 || (DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR < 4)
#error "berkeley db 4.4+ required"
#endif

struct db4env {
    DB_ENV *env;
    int dirfd;
    struct db4db *db4list;
    char dirname[1];
};

struct db4db {
    DB *db;
    struct db4db *next;
    unsigned hash;
    char dbname[1];
};

#define ERROR(fmt, args...) \
    fprintf(stderr, "%s: %s: " fmt "\n", \
	    program_invocation_short_name, __func__, ##args)

static
void errcall(const DB_ENV *env, const char *prefix, const char *msg)
{
    (void) env;
    (void) prefix;
    ERROR("%s", msg);
}

static
void msgcall(const DB_ENV *env, const char *msg)
{
    (void) env;
    ERROR("%s", msg);
}

static
#if DB_VERSION_MAJOR > 4 || (DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR >= 5)
int isalive(DB_ENV *env, pid_t pid, db_threadid_t tid, u_int32_t flags)
#else
int isalive(DB_ENV *env, pid_t pid, db_threadid_t tid)
#endif
{
    (void) env;
    (void) tid;
#if DB_VERSION_MAJOR > 4 || (DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR >= 5)
    (void) flags;
#endif
    int rc = kill(pid, 0);
    if (rc == 0)
	return 1;
    if (errno == ESRCH)
	return 0;
    return 1;
}

struct db4env *db4env_open(const char *dirname)
{
    int rc;

    // 1: allocate env
    struct db4env *env4 = malloc(sizeof(*env4) + strlen(dirname));
    if (env4 == NULL) {
	ERROR("malloc: %m");
	return NULL;
    }
    strcpy(env4->dirname, dirname);
    env4->db4list = NULL;

    // 2: allocate env
    rc = db_env_create(&env4->env, 0);
    if (rc) {
	ERROR("env_create: %s", db_strerror(rc));
	// 1
	free(env4);
	return NULL;
    }

    // configure env
    env4->env->set_errcall(env4->env, errcall);
    env4->env->set_msgcall(env4->env, msgcall);
    env4->env->set_isalive(env4->env, isalive);
    env4->env->set_thread_count(env4->env, 16);

    // 3: open dir
    env4->dirfd = open(env4->dirname, O_RDONLY | O_DIRECTORY);
    if (env4->dirfd < 0) {
	ERROR("open %s: %m", env4->dirname);
	// 2
	env4->env->close(env4->env, 0);
	// 1
	free(env4);
	return NULL;
    }

    // 4: lock dir
    do
	rc = flock(env4->dirfd, LOCK_EX);
    while (rc < 0 && errno == EINTR);
    if (rc) {
	ERROR("LOCK_EX %s: %m", env4->dirname);
	// 3
	close(env4->dirfd);
	// 2
	env4->env->close(env4->env, 0);
	// 1
	free(env4);
	return NULL;
    }

    // 5: block signals
    sigset_t set, oldset;
    sigfillset(&set);
    sigprocmask(SIG_BLOCK, &set, &oldset);

    // open env
    rc = (env4->env->open)(env4->env, env4->dirname,
	    DB_CREATE | DB_INIT_CDB | DB_INIT_MPOOL, 0);
    if (rc) {
	ERROR("env_open %s: %s", env4->dirname, db_strerror(rc));
	// 5
	sigprocmask(SIG_SETMASK, &oldset, NULL);
	// 4
	flock(env4->dirfd, LOCK_UN);
	// 3
	close(env4->dirfd);
	// 2
	env4->env->close(env4->env, 0);
	// 1
	free(env4);
	return NULL;
    }

    // run recovery
    rc = env4->env->failchk(env4->env, 0);
    if (rc) {
	ERROR("env_failchk %s: %s", env4->dirname, db_strerror(rc));
	// 5
	sigprocmask(SIG_SETMASK, &oldset, NULL);
	// 4
	flock(env4->dirfd, LOCK_UN);
	// 3
	close(env4->dirfd);
	// 2
	env4->env->close(env4->env, 0);
	// 1
	free(env4);
	return NULL;
    }

    sigprocmask(SIG_SETMASK, &oldset, NULL);
    flock(env4->dirfd, LOCK_UN);

    return env4;
}

void db4env_close(struct db4env *env4)
{
    if (env4 == NULL)
	return;

    int rc;

    // lock dir
    do
	rc = flock(env4->dirfd, LOCK_EX);
    while (rc < 0 && errno == EINTR);
    if (rc)
	ERROR("LOCK_EX %s: %m", env4->dirname);

    // block signals
    sigset_t set, oldset;
    sigfillset(&set);
    sigprocmask(SIG_BLOCK, &set, &oldset);

    // close databases
    while (env4->db4list) {
	struct db4db *db4 = env4->db4list;
	env4->db4list = db4->next;
	rc = db4->db->close(db4->db, 0);
	if (rc)
	    ERROR("db_close %s: %s", db4->dbname, db_strerror(rc));
	free(db4);
    }

    // close env
    rc = env4->env->close(env4->env, 0);
    if (rc)
	ERROR("env_close %s: %s", env4->dirname, db_strerror(rc));

    sigprocmask(SIG_SETMASK, &oldset, NULL);
    flock(env4->dirfd, LOCK_UN);

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

struct db4db *db4env_db(struct db4env *env4,
	const char *dbname, unsigned flags)
{
    // see if db is already open
    unsigned hash = strhash(dbname);
    struct db4db *db4 = env4->db4list, *prev = NULL;
    while (db4) {
	if (hash == db4->hash && strcmp(dbname, db4->dbname) == 0) {
	    // found, move to front
	    if (db4 != env4->db4list) {
		prev->next = db4->next;
		db4->next = env4->db4list;
		env4->db4list = db4;
	    }
	    return db4;
	}
	prev = db4;
	db4 = db4->next;
    }

    // 1: allocate db4
    int len = strlen(dbname);
    db4 = malloc(sizeof(*db4) + len + sizeof(".db") - 1);
    if (db4 == NULL) {
	ERROR("malloc: %m");
	return NULL;
    }

    // 2: allocate db
    int rc = db_create(&db4->db, env4->env, 0);
    if (rc) {
	ERROR("db_create: %s", db_strerror(rc));
	// 1
	free(db4);
	return NULL;
    }

    // prepare filename
    db4->hash = hash;
    strcpy(db4->dbname, dbname);
    strcpy(db4->dbname + len, ".db");

    // lock dir
    do
	rc = flock(env4->dirfd, LOCK_EX);
    while (rc < 0 && errno == EINTR);
    if (rc) {
	ERROR("LOCK_EX %s: %m", env4->dirname);
	// 2
	db4->db->close(db4->db, 0);
	// 1
	free(db4);
	return NULL;
    }

    // block signals
    sigset_t set, oldset;
    sigfillset(&set);
    sigprocmask(SIG_BLOCK, &set, &oldset);

    // open db
    rc = db4->db->open(db4->db, NULL, db4->dbname, NULL,
	    /*DB_BTREE*/ flags, DB_CREATE, 0666);

    sigprocmask(SIG_SETMASK, &oldset, NULL);
    flock(env4->dirfd, LOCK_UN);

    if (rc) {
	ERROR("db_open: %s", db_strerror(rc));
	// 2
	db4->db->close(db4->db, 0);
	// 1
	free(db4);
	return NULL;
    }

    // truncate ".db" suffix
    db4->dbname[len] = '\0';

    // push to front
    db4->next = env4->db4list;
    env4->db4list = db4;

    return db4;
}

bool db4db_get(struct db4db *db4,
	const void *key, int keysize,
	const void **datap, int *datasizep)
{
    DBT k = { (void *)key, keysize }, d = { 0, 0 };
    int rc = db4->db->get(db4->db, NULL, &k, &d, 0);
    if (rc) {
	if (rc != DB_NOTFOUND)
	    ERROR("db_get %s: %s", db4->dbname, db_strerror(rc));
	return false;
    }

    if (datap)
	*datap = d.data;
    if (datasizep)
	*datasizep = d.size;
    return true;
}

bool db4db_put(struct db4db *db4,
	const void *key, int keysize,
	const void *data, int datasize)
{
    sigset_t set, oldset;
    sigfillset(&set);
    sigprocmask(SIG_BLOCK, &set, &oldset);

    DBT k = { (void *)key, keysize }, d = { (void *)data, datasize };
    int rc = db4->db->put(db4->db, NULL, &k, &d, 0);

    sigprocmask(SIG_SETMASK, &oldset, NULL);

    if (rc) {
	ERROR("db_put %s: %s", db4->dbname, db_strerror(rc));
	return false;
    }
    return true;
}

// ex:ts=8 sts=4 sw=4 noet
