Header hdrcache_get(const char *bn, const struct stat *st, unsigned *off);
void hdrcache_put(const char *bn, const struct stat *st, Header h, unsigned off);
