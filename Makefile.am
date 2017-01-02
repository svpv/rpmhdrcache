AM_CFLAGS = -Wall -Wextra -D_GNU_SOURCE

lib_LTLIBRARIES = librpmcache.la rpmhdrcache.la
librpmcache_la_SOURCES = cache.c db.c fs.c bsm.c rpmcache.c
librpmcache_la_LIBADD = -ldb -lcrypto -lzstd
librpmcache_la_LDFLAGS = -no-undefined -Wl,--no-undefined

rpmhdrcache_la_SOURCES = preload.c hdrcache.c
rpmhdrcache_la_LIBADD = librpmcache.la -lrpmdb -ldl
rpmhdrcache_la_LDFLAGS = -module -avoid-version -shared \
	-Wl,--no-as-needed -lrpmio -lrpm -Wl,--as-needed -no-undefined -Wl,--no-undefined

include_HEADERS = rpmcache.h

otherincludedir = $(includedir)/qa
otherinclude_HEADERS = cache.h

bin_PROGRAMS = qacache-clean
qacache_clean_SOURCES = clean.c
qacache_clean_LDADD = librpmcache.la

rpmcache.lo: rpmarch.h
rpmarch.h: rpmarch.gperf
	gperf <$< >$@
