#ifndef DB4_H
#define DB4_H

#ifdef __cplusplus
extern "C" {
#endif

struct db4env *db4env_open(const char *dirname);
void db4env_close(struct db4env *env4);

enum {
    DB4_BTREE = 1,
    DB4_HASH = 2,
};
struct db4db *db4env_db(struct db4env *env4,
	const char *dbname, unsigned dbflags);

#ifndef __cplusplus
#include <stdbool.h>
#endif

bool db4db_get(struct db4db *db4,
	const void *key, int keysize,
	const void **datap, int *datasizep);
bool db4db_put(struct db4db *db4,
	const void *key, int keysize,
	const void *data, int datasize);

#ifdef __cplusplus
}
#endif

#endif
