AC_INIT([qacache], [0.5])
AC_CONFIG_SRCDIR([cache.c])

AM_INIT_AUTOMAKE([foreign])
AM_PROG_LIBTOOL

AC_PROG_CC_C99
if test "x$ac_cv_prog_cc_c99" = xno ; then
	AC_MSG_ERROR([ISO C99 capable compiler required])
fi

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
