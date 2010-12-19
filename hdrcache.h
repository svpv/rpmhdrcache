Header hdrcache_get(const char *path, struct stat st, unsigned *off);
void hdrcache_put(const char *path, struct stat st, Header h, unsigned off);
