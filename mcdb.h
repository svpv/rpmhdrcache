#ifndef mcdb_H
#define mcdb_H

#ifdef __cplusplus
extern "C" {
#endif

struct mcdbenv *mcdbenv_open(const char *dirname);
void mcdbenv_close(struct mcdbenv *env);

#ifndef __cplusplus
#include <stdbool.h>
#endif

bool mcdb_get(struct mcdbenv *env,
	const void *key, int keysize,
	const void **datap, size_t *datasizep);
bool mcdb_put(struct mcdbenv *env,
	const void *key, int keysize,
	const void *data, size_t datasize);

#ifdef __cplusplus
}
#endif

#endif
