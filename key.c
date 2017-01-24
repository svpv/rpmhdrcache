#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <endian.h>
#include <rpm/rpmlib.h>
#include "hdrcache.h"
#include "sm3.h"

bool hdrcache_key(const char *fname, const struct stat *st, struct key *key)
{
    const char *bn = strrchr(fname, '/');
    bn = bn ? bn + 1 : fname;
    // validate basename
    size_t len = strlen(bn);
    if (len < sizeof("a-1-1.src.rpm") - 1)
	return false;
    const char *dotrpm = bn + len - 4;
    if (memcmp(dotrpm, ".rpm", 4))
	return false;
    // size and mtime will be serialized into 8 base64 characters;
    // also, ".rpm" suffix will be stripped; on the second thought,
    // a separator should rather be kept
    key->len = len + 8 - 3;
    if (key->len > MAXKEYLEN) {
	fprintf(stderr, "%s %s: name too long\n", __func__, bn);
	return 0;
    }
    // copy basename and put the separator
    char *p = mempcpy(key->str, bn, len - 4);
    *p++ = '@';
    // combine size+mtime
#if __BYTE_ORDER == __LITTLE_ENDIAN
    uint64_t sm48 = 0;
    sm3(st->st_size, st->st_mtime, (unsigned short *) &sm48);
#else
    unsigned short sm[3];
    sm3(st->st_size, st->st_mtime, sm);
    uint64_t sm48 = htole16(sm[0]) |
	    // when htole16(x) returns unsigned short, sm[1] will be
	    // promoted to int on behalf of << operator; it is crucial
	    // to cast sm[1] to unsigned, to prune sign extenstion
	    ((uint32_t) htole16(sm[1]) << 16) |
	    ((uint64_t) htole16(sm[2]) << 32) ;
#endif
    // serialize size+mtime with base64
    static const char base64[] = "0123456789"
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	    "abcdefghijklmnopqrstuvwxyz" "+/";
    for (int i = 0; i < 8; i++, sm48 >>= 6)
	*p++ = base64[sm48 & 077];
    *p = '\0';
    return true;
}
