AM_CFLAGS = -Wall -Wextra -D_GNU_SOURCE
lib_LTLIBRARIES = libqacache.la
libqacache_la_SOURCES = cache.c db.c fs.c
libqacache_la_LIBADD = -ldb -lcrypto -lzstd
libqacache_la_LDFLAGS = -no-undefined -Wl,--no-undefined

bin_PROGRAMS = qacache-clean
qacache_clean_SOURCES = clean.c
qacache_clean_LDADD = libqacache.la
