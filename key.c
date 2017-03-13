#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include "rpmcache.h"
#include "sm3.h"

bool rpmcache_key(const char *fname, unsigned fsize, unsigned mtime, struct rpmkey *key)
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
    // size and mtime will be serialized into 9 base62 characters;
    // also, ".rpm" suffix will be stripped; on the second thought,
    // a separator should rather be kept
    key->len = len + 9 - 3;
    if (key->len > MAXRPMKEYLEN) {
	fprintf(stderr, "%s %s: name too long\n", __func__, bn);
	return false;
    }
    // copy basename and put the separator
    char *p = mempcpy(key->str, bn, len - 4);
    *p++ = '@';
    // serialize size+mtime
    // since 2^53 < 62^9, 53 bits fit nicely into 9 base62 characters
    uint64_t sm53 = smx(fsize, mtime, 53);
    static const char base62[] = "0123456789"
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	    "abcdefghijklmnopqrstuvwxyz";
    for (int i = 0; i < 9; i++) {
	int r = sm53 % 62;
	sm53 /= 62;
	*p++ = base62[r];
    }
    assert(sm53 == 0);
    *p = '\0';
    return true;
}
