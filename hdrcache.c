#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <rpm/rpmlib.h>
#include "hdrcache.h"

#include <stdio.h>

Header hdrcache_get(const char *path, struct stat st, unsigned *off)
{
    fprintf(stderr, "%s %s\n", __func__, path);
    return NULL;
}

void hdrcache_put(const char *path, struct stat st, Header h, unsigned off)
{
    fprintf(stderr, "%s %s\n", __func__, path);
}

// ex: set ts=8 sts=4 sw=4 noet:
