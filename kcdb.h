#ifndef KCDB_H
#define KCDB_H

#ifdef __cplusplus
extern "C" {
#endif

struct kcdbenv *kcdbenv_open(const char *dirname);
void kcdbenv_close(struct kcdbenv *env4);

enum {
    DB4_BTREE = 1,
    DB4_HASH = 2,
};
struct kcdbdb *kcdbenv_db(struct kcdbenv *env4, const char *dbname);

#ifndef __cplusplus
#include <stdbool.h>
#endif

bool kcdbdb_get(struct kcdbdb *kcdb,
	const void *key, int keysize,
	const void **datap, int *datasizep);
bool kcdbdb_put(struct kcdbdb *kcdb,
	const void *key, int keysize,
	const void *data, int datasize);

#ifdef __cplusplus
}
#endif

#endif
