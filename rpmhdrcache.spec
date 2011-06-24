Name: rpmhdrcache
Version: 0.1
Release: alt0

Summary: Cached reading of rpm package headers
License: GPLv2+
Group: System/Configuration/Packaging

URL: http://git.altlinux.org/gears/r/rpmhdrcache.git
Source: %name-%version.tar

Requires: libqacache = %version-%release

# Automatically added by buildreq on Thu Jun 23 2011
BuildRequires: libdb4-devel librpm-devel libsnappy-devel libssl-devel

%description
Sisyphus repository currently has more than 10K source packages (which is
more than 60K rpm files with subpackages).  To assist repeated repo scanning
(which is required for each repo update), this package provides rpmhdrcache.so
perloadable module.  This module intercepts rpmReadPackageHeader calls and
caches the result using libqacache library.

%prep
%setup -q

%build
gcc -shared -fPIC -D_GNU_SOURCE %optflags -o libqacache.so.0 -Wl,-soname,libqacache.so.0 cache.c \
	-ldb -lcrypto -lsnappy -Wl,-z,defs
gcc -shared -fPIC -D_GNU_SOURCE %optflags -o rpmhdrcache.so preload.c hdrcache.c \
	-Wl,--no-as-needed -lrpmio -lrpm -Wl,--as-needed -lrpmdb -ldl libqacache.so.0 -Wl,-z,defs

%install
install -pD -m644 cache.h %buildroot%_includedir/qa/cache.h
install -pD -m644 libqacache.so.0 %buildroot%_libdir/libqacache.so.0
ln -s libqacache.so.0 %buildroot%_libdir/libqacache.so
install -pD -m644 rpmhdrcache.so %buildroot%_libdir/rpmhdrcache.so

%files
%_libdir/rpmhdrcache.so

%package -n libqacache
Summary: NoSQL solution for data caching
Group: System/Libraries

%package -n libqacache-devel
Summary: NoSQL solution for data caching
Group: Development/C
Requires: libqacache = %version-%release

%description -n libqacache
This library implements simple key-value cache API with limited support
for concurrent reads, atomic writes, data integrity, and atime cleanup.
Small- to medium-sized cache entries (up to 32K compressed with snappy)
are stored in a Berkeley DB, larger entries are backed by filesystem.

%description -n libqacache-devel
This library implements simple key-value cache API with limited support
for concurrent reads, atomic writes, data integrity, and atime cleanup.
Small- to medium-sized cache entries (up to 32K compressed with snappy)
are stored in a Berkeley DB, larger entries are backed by filesystem.

%files -n libqacache
%_libdir/libqacache.so.0

%files -n libqacache-devel
%dir %_includedir/qa
%_includedir/qa/cache.h
%_libdir/libqacache.so
