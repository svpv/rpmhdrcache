#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "conf.h"

struct conf *findconf(FILE *fp, const char *name)
{
    char line[128], *s;
    enum conftype t;
    size_t namelen = strlen(name);
    while ((s = fgets(line, sizeof line, fp))) {
	if (strncmp(s, name, namelen))
	    continue;
	s += namelen;
	if (!isspace(*s))
	    continue;
	do s++; while (isspace(*s));
	switch (*s) {
	case 'q':
	    if (strncmp(s, "qacache", sizeof("qacache") - 1))
		continue;
	    s += sizeof("qacache") - 1;
	    if (!isspace(*s))
		continue;
	    t = CONFTYPE_QACACHE;
	    break;
	case 'm':
	    if (strncmp(s, "memcached", sizeof("memcached") - 1))
		continue;
	    s += sizeof("memcached") - 1;
	    if (!isspace(*s))
		continue;
	    t = CONFTYPE_MEMCACHED;
	    break;
	case 'r':
	    if (strncmp(s, "redis", sizeof("redis") - 1))
		continue;
	    s += sizeof("redis") - 1;
	    if (!isspace(*s))
		continue;
	    t = CONFTYPE_REDIS;
	    break;
	default: continue;
	}
	do s++; while (isspace(*s));
	char *q = s;
	while (*s && *s != '\n')
	    s++;
	if (s == q)
	    continue;
	struct conf *c = malloc(sizeof(*c) + s - q + 1);
	c->t = t;
	memcpy(c->str, q, s - q);
	c->str[s - q] = '\0';
	return c;
    }
    return NULL;
}
