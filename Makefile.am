# -D_GNU_SOURCE is needed for RTLD_NEXT and program_invocation_short_name
# -std=gnu11 works with gcc >= 4.7, while RHEL7 has gcc-4.8
AM_CFLAGS = -Wall -Wextra -D_GNU_SOURCE -std=gnu11

lib_LTLIBRARIES = librpmcache.la rpmhdrcache.la
librpmcache_la_SOURCES = cache.c db.c fs.c bsm.c rpmcache.c key.c
librpmcache_la_LIBADD = -ldb -lcrypto -lzstd
librpmcache_la_LDFLAGS = -no-undefined -Wl,--no-undefined

rpmhdrcache_la_SOURCES = preload.c hdrcache.c mcdb.c
# the only exported symbol, exclicitly marked as such, is rpmReadPackageFile
rpmhdrcache_la_CFLAGS = $(AM_CFLAGS) -fvisibility=hidden
# rpmhdrcache.so should be linked with -lrpm, because it calls
# rpmReadPackageFile (via dlsym(RTLD_NEXT, __func__); the call becomes
# problematic with e.g. Perl's RPM.so, because Perl loads it with RTLD_LOCAL
rpmhdrcache_la_LIBADD = librpmcache.la -lrpm -ldl -llz4 -lmemcached
rpmhdrcache_la_LDFLAGS = -module -avoid-version -shared \
	-Wl,--no-as-needed -lrpmio -lrpm -Wl,--as-needed \
	-no-undefined -Wl,--no-undefined

include_HEADERS = rpmcache.h

otherincludedir = $(includedir)/qa
otherinclude_HEADERS = cache.h

bin_PROGRAMS = qacache-clean
qacache_clean_SOURCES = clean.c
qacache_clean_LDADD = librpmcache.la

rpmcache.lo: rpmarch.h
rpmarch.h: rpmarch.gperf
	gperf <$< >$@
