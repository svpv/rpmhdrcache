// This is a tiny wrapper around libmemcached, which can
// hopefully reduce the complexity of interacting with
// memcached to only a few well-defined operations.

struct mcdb *mcdb_open(const char *configstring);
void mcdb_close(struct mcdb *db);

bool mcdb_get(struct mcdb *db,
	const char *key, size_t keylen,
	const void **datap, size_t *datasizep);
void mcdb_put(struct mcdb *db,
	const char *key, size_t keylen,
	const void *data, size_t datasize);

int mcdb_max_item_size(struct mcdb *db);
