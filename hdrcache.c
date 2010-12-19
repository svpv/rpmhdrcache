#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <rpm/rpmlib.h>
#include <lzo/lzo1x.h>
#include "hdrcache.h"
#include "db4.h"

static __thread
struct db4env *env4;

static
void finalize()
{
    db4env_close(env4);
}

static __thread
unsigned short now; // binary days since the epoch

static
int initialize()
{
    static __thread
    int initialized;
    if (initialized)
	return initialized;
    env4 = db4env_open("/tmp/.rpmhdrcache");
    if (env4 == NULL) {
	initialized = -1;
	return initialized;
    }
    initialized = 1;
    atexit(finalize);
    now = (time(NULL) >> 16);
    lzo_init();
    return initialized;
}

enum {
    V_ZBIT = (1 << 0), // the first bit must be zero
    V_STO  = (1 << 1), // perl Storable
    V_LZO  = (1 << 2), // compressed with lzo
    V_ZLIB = (1 << 3), // compressed with zlib
    V_RSV  = (1 << 4), // has reserved field
};

struct cache_ent {
    // cache info
    unsigned short vflags;
    unsigned short mtime;
    unsigned short atime;
    unsigned short reserved;
    // user data
    unsigned off;
    char blob[1];
};

static
int make_key(const char *path, struct stat st, char *key)
{
    const char *bn = strrchr(path, '/');
    bn = bn ? (bn + 1) : path;
    strcpy(key, bn);
    unsigned sm[2] = { st.st_size, st.st_mtime };
    int len = strlen(bn);
    memcpy(key + len + 1, sm, sizeof sm);
    return len + 1 + sizeof sm;
}

#include <stdio.h>

static
const int hdrsize_max = (256 << 10);

Header hdrcache_get(const char *path, struct stat st, unsigned *off)
{
    if (initialize() < 0)
	return NULL;
    struct db4db *db4 = db4env_db(env4, "cache", DB4_BTREE);
    if (db4 == NULL)
	return NULL;
    char key[4096];
    int keysize = make_key(path, st, key);
    struct cache_ent *data;
    int datasize;
    bool got = db4db_get(db4, key, keysize, &data, &datasize);
    if (!got)
	return NULL;
    if ((data->vflags & (V_ZBIT | V_RSV)) != V_RSV)
	return NULL;
    // atime == 0: atime update disabled
    // atime == 1: recently added object
    if ((data->atime > 1 && data->atime < now) ||
	(data->atime == 1 && data->mtime < now))
    {
	data->atime = now;
	db4db_put(db4, key, keysize, data, datasize);
    }
    void *blob = data->blob;
    char ublob[hdrsize_max];
    if (data->vflags & V_LZO) {
	int blobsize = datasize - sizeof(struct cache_ent) + 1;
	lzo_uint ublobsize = 0;
	int rc = lzo1x_decompress(blob, blobsize, ublob, &ublobsize, NULL);
	if (rc != LZO_E_OK || ublobsize < 1 || ublobsize > hdrsize_max) {
	    fprintf(stderr, "%s %s: lzo1x_decompress failed\n", __func__, key);
	    return NULL;
	}
	blob = ublob;
    }
    Header h = headerLoad(blob);
    if (h == NULL) {
	fprintf(stderr, "%s %s: headerLoad failed\n", __func__, key);
	return NULL;
    }
    if (off)
	*off = data->off;
    return h;
}

void hdrcache_put(const char *path, struct stat st, Header h, unsigned off)
{
    if (initialize() < 0)
	return;
    struct db4db *db4 = db4env_db(env4, "cache", DB4_BTREE);
    if (db4 == NULL)
	return;
    int hdrsize = headerSizeof(h, HEADER_MAGIC_NO);
    if (hdrsize < 1 || hdrsize > hdrsize_max)
	return;
    char key[4096];
    int keysize = make_key(path, st, key);
    int databufsize = sizeof(struct cache_ent) - 1 +
	    hdrsize + hdrsize / 16 + 64 + 3;
    char databuf[databufsize];
    int datasize;
    struct cache_ent *data = (void *) databuf;
    data->vflags = V_RSV;
    data->mtime = now;
    data->atime = 1;
    data->reserved = 0;
    data->off = off;
    void *blob = headerUnload(h);
    if (blob == NULL) {
	fprintf(stderr, "%s %s: headerLoad failed\n", __func__, key);
	return;
    }
    char lzobuf[LZO1X_1_MEM_COMPRESS];
    lzo_uint lzosize = 0;
    lzo1x_1_compress(blob, hdrsize, data->blob, &lzosize, lzobuf);
    if (lzosize > 0 && lzosize < hdrsize) {
	data->vflags |= V_LZO;
	datasize = sizeof(struct cache_ent) - 1 + lzosize;
    }
    else {
	memcpy(data->blob, blob, hdrsize);
	datasize = sizeof(struct cache_ent) - 1 + hdrsize;
    }
    free(blob);
    db4db_put(db4, key, keysize, data, datasize);
}

// ex: set ts=8 sts=4 sw=4 noet:
