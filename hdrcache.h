#include "rpmcache.h"
Header hdrcache_get(const struct rpmkey *key, unsigned *off);
void hdrcache_put(const struct rpmkey *key, Header h, unsigned off);
