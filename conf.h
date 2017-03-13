enum conftype {
    CONFTYPE_QACACHE,
    CONFTYPE_MEMCACHED,
    CONFTYPE_REDIS,
};

struct conf {
    enum conftype t;
    char str[];
};

struct conf *findconf(FILE *fp, const char *name);
