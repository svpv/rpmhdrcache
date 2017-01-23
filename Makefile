RPM_OPT_FLAGS ?= -g -O2
# A few notes on flags:
# 1) -D_GNU_SOURCE is needed for RTLD_NEXT and program_invocation_short_name;
# 2) -fwhole-program is a stronger form of -fvisibility=hidden;
# the only exported symbol, exclicitly marked as such, is rpmReadPackageFile;
# 3) rpmhdrmemcache.so should be linked with -lrpm, because it calls
# rpmReadPackageFile (via dlsym(RTLD_NEXT, __func__); the call becomes
# problematic with e.g. Perl's RPM.so, because Perl loads it with RTLD_LOCAL;
# 4) large file support is not actually needed, because in the code,
# st_size and st_mtime are always truncated to 32-bit unsigned integers;
# 5) -std=gnu11 works with gcc >= 4.7, while RHEL7 has gcc-4.8.
rpmhdrmemcache.so: preload.c key.c hdrcache.c mcdb.c
	$(CC) -o $@ $^ $(RPM_OPT_FLAGS) \
		-std=gnu11 -D_GNU_SOURCE -Wall \
		-fpic -shared -flto -fwhole-program \
		-Wl,--no-as-needed -lrpm -Wl,--as-needed -lrpmio \
		-ldl -lmemcached -lmemcachedutil -llzo2 -Wl,-z,defs
