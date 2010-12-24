Name: rpmhdrcache
Version: 0.1
Release: alt0

Summary: Cached reading of rpm package headers
License: GPLv2+
Group: System/Configuration/Packaging

URL: http://git.altlinux.org/gears/r/rpmhdrcache.git
Source: %name-%version.tar

# Automatically added by buildreq on Fri Dec 24 2010
BuildRequires: libdb4-devel liblzo2-devel librpm-devel

%description

%prep
%setup -q

%build
gcc -shared -fPIC -D_GNU_SOURCE %optflags -combine -fwhole-program \
	-o rpmhdrcache.so preload.c hdrcache.c db4.c \
	-Wl,--no-as-needed -lrpmio -lrpm -Wl,--as-needed -lrpmdb -ldl -ldb -llzo2 -Wl,-z,defs

%install
install -pD -m644 rpmhdrcache.so %buildroot%_libdir/rpmhdrcache.so

%files
%_libdir/rpmhdrcache.so
