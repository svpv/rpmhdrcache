AM_CFLAGS = -Wall -Wextra -D_GNU_SOURCE
lib_LTLIBRARIES = libqacache.la rpmhdrcache.la
libqacache_la_SOURCES = cache.c db.c fs.c rpmcache.c
libqacache_la_LIBADD = -ldb -lcrypto -lzstd
libqacache_la_LDFLAGS = -no-undefined -Wl,--no-undefined

rpmhdrcache_la_SOURCES = preload.c hdrcache.c
rpmhdrcache_la_LIBADD = libqacache.la -lrpmdb -ldl
rpmhdrcache_la_LDFLAGS = -module -avoid-version -shared \
	-Wl,--no-as-needed -lrpmio -lrpm -Wl,--as-needed -no-undefined -Wl,--no-undefined

otherincludedir = $(includedir)/qa
otherinclude_HEADERS = cache.h rpmcache.h

bin_PROGRAMS = qacache-clean
qacache_clean_SOURCES = clean.c
qacache_clean_LDADD = libqacache.la
