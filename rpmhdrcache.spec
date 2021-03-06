Name: rpmhdrcache
Version: 0.6
Release: alt1

Summary: Cached reading of rpm package headers
License: GPLv2+
Group: System/Configuration/Packaging

URL: http://git.altlinux.org/gears/r/rpmhdrcache.git
Source: %name-%version.tar

Requires: librpmcache = %version-%release

# Automatically added by buildreq on Tue Nov 15 2016
BuildRequires: gperf libdb4-devel librpm-devel libssl-devel libzstd-devel

%description
Sisyphus repository currently has more than 10K source packages (which is
more than 60K rpm files with subpackages).  To assist repeated repo scanning
(which is required for each repo update), this package provides rpmhdrcache.so
perloadable module.  This module intercepts rpmReadPackageHeader calls and
caches the result using librpmcache library.

%prep
%setup -q

%build
autoreconf -fisv
%configure --disable-static
make

%install
make install DESTDIR=%buildroot
ln -s librpmcache.so %buildroot%_libdir/libqacache.so

%files
%_libdir/rpmhdrcache.so

%package -n librpmcache
Summary: NoSQL solution for data caching
Group: System/Libraries
# due to qacache-clean
Conflicts: libqacache

%package -n librpmcache-devel
Summary: NoSQL solution for data caching
Group: Development/C
Requires: librpmcache = %version-%release
Provides: libqacache-devel = %version
Obsoletes: libqacache-devel < %version

%description -n librpmcache
This library implements simple key-value cache API with limited support
for concurrent reads, atomic writes, data integrity, and atime cleanup.
Small- to medium-sized cache entries (up to 32K compressed with snappy)
are stored in a Berkeley DB, larger entries are backed by filesystem.

%description -n librpmcache-devel
This library implements simple key-value cache API with limited support
for concurrent reads, atomic writes, data integrity, and atime cleanup.
Small- to medium-sized cache entries (up to 32K compressed with snappy)
are stored in a Berkeley DB, larger entries are backed by filesystem.

%files -n librpmcache
%_libdir/librpmcache.so.0*
%_bindir/qacache-clean

%files -n librpmcache-devel
%dir %_includedir/qa
%_includedir/qa/cache.h
%_includedir/rpmcache.h
%_libdir/libqacache.so
%_libdir/librpmcache.so

%changelog
* Tue Oct 04 2011 Alexey Tourbin <at@altlinux.ru> 0.3-alt1
- cache.c: implemented db and fs atime cleanup
- clean.c: implemented /usr/bin/qacache-clean helper program

* Thu Sep 08 2011 Alexey Tourbin <at@altlinux.ru> 0.2.3-alt1
- don't close cache db in child processes

* Wed Sep 07 2011 Alexey Tourbin <at@altlinux.ru> 0.2.2-alt1
- implemented db atime update using DB_DBT_PARTIAL

* Mon Jun 27 2011 Alexey Tourbin <at@altlinux.ru> 0.2.1-alt1
- when fetching empty value, cache_get will set *valp to NULL
- otherwise, trailing null byte will be added past the end of value

* Fri Jun 24 2011 Alexey Tourbin <at@altlinux.ru> 0.2-alt1
- version 0.1 was not released properly
- factored libqacache library (similar to qa::cache perl module)
- replaced LZO compression with snappy
- replaced CDS with dir flock
- replaced Btree with SHA1-based hash DB
- implemented filesystem-backed store for large cache entries
- atime update and cleanup not cooked up yet
