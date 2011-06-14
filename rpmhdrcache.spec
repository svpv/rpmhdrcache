Name: rpmhdrcache
Version: 0.1
Release: alt0.1

Summary: Cached reading of rpm package headers
License: GPLv2+
Group: System/Configuration/Packaging

URL: http://git.altlinux.org/gears/r/rpmhdrcache.git
Source: %name-%version.tar

# Automatically added by buildreq on Fri Dec 24 2010
BuildRequires: libkyotocabinet-devel liblzo2-devel librpm-devel

%description

%prep
%setup -q

%build
gcc -shared -fPIC -D_GNU_SOURCE %optflags -combine -fwhole-program \
	-o rpmhdrcache.so preload.c hdrcache.c kcdb.c \
	-Wl,--no-as-needed -lrpmio -lrpm -Wl,--as-needed -lrpmdb -ldl -lkyotocabinet -llzo2 -Wl,-z,defs 

%install
install -pD -m644 rpmhdrcache.so %buildroot%_libdir/rpmhdrcache.so

%files
%_libdir/rpmhdrcache.so

%changelog
* Fri Jun 10 2011 Vitaly Kuznetsov <vitty@altlinux.ru> 0.1-alt0.1
- quick&dirty implementation using kyotocabinet

