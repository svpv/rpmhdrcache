// memcached needs ASCII keys no longer than 250 characters
#define MAXKEYLEN 250

struct key {
    size_t len;
    char str[MAXKEYLEN+1];
};

bool hdrcache_key(const char *fname, const struct stat *st, struct key *key);

Header hdrcache_get(const struct key *key, unsigned *off);
void hdrcache_put(const struct key *key, Header h, unsigned off);
