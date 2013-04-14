AC_INIT([frida-npapi], [1.0.0])
AC_CONFIG_SRCDIR([src/plugin.cpp])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_SYSTEM

AM_INIT_AUTOMAKE([-Wall -Werror no-dist-gzip dist-bzip2 foreign])

# Enable silent rules if available
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AM_MAINTAINER_MODE

AC_PROG_CXX
AM_PROG_AR
AC_PROG_INSTALL
LT_INIT

PKG_CHECK_MODULES(NPFRIDA, ["frida-core-1.0"])
AC_SUBST(NPFRIDA_CFLAGS)
AC_SUBST(NPFRIDA_LDFLAGS)
AC_SUBST(NPFRIDA_LIBS)

AC_CONFIG_FILES([
  Makefile
  src/Makefile
])
AC_OUTPUT
