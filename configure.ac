AC_INIT([frida-npapi], [1.0.0])
AC_CONFIG_SRCDIR([src/npfrida-plugin.cpp])
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
AM_PROG_VALAC([0.10.0])
AC_PROG_INSTALL
LT_INIT

if echo $CC | grep -q "gcc"; then
  # We don't care that much about GCC warnings as we cannot silence all of them
  # for the Vala-generated code...
  CFLAGS="$CFLAGS -w"
fi

case $host_os in
  linux*)  HAVE_LINUX=yes  ;;
  darwin*) HAVE_DARWIN=yes ;;
  *)                       ;;
esac
if [[ "x$HAVE_LINUX" = "xyes" ]]; then
  AC_DEFINE(HAVE_LINUX, 1, [Define to 1 if OS is Linux based.])
fi
if [[ "x$HAVE_DARWIN" = "xyes" ]]; then
  AC_DEFINE(HAVE_DARWIN, 1, [Define to 1 if OS is Darwin based.])
fi
AM_CONDITIONAL(OS_LINUX, [test "x$HAVE_LINUX" = "xyes"])
AM_CONDITIONAL(OS_DARWIN, [test "x$HAVE_DARWIN" = "xyes"])

case $host_os in
  darwin*)
    case $host_cpu in
      arm|aarch64)
        HAVE_MAC=no
        HAVE_IOS=yes
        ;;
      *)
        HAVE_MAC=yes
        HAVE_IOS=no
        ;;
    esac
    ;;
  *)
    ;;
esac
if [[ "x$HAVE_MAC" = "xyes" ]]; then
  AC_DEFINE(HAVE_MAC, 1, [Define to 1 if OS is Mac.])
fi
AM_CONDITIONAL(OS_MAC, [test "x$HAVE_MAC" = "xyes"])

if [[ "x$HAVE_IOS" = "xyes" ]]; then
  AC_DEFINE(HAVE_IOS, 1, [Define to 1 if OS is iOS.])
fi
AM_CONDITIONAL(OS_IOS, [test "x$HAVE_IOS" = "xyes"])

NPFRIDA_VALAFLAGS=
if [[ "x$HAVE_LINUX" = "xyes" ]]; then
  NPFRIDA_VALAFLAGS="$NPFRIDA_VALAFLAGS -D LINUX"
fi
if [[ "x$HAVE_DARWIN" = "xyes" ]]; then
  NPFRIDA_VALAFLAGS="$NPFRIDA_VALAFLAGS -D DARWIN"
fi
if [[ "x$HAVE_MAC" = "xyes" ]]; then
  NPFRIDA_VALAFLAGS="$NPFRIDA_VALAFLAGS -D MAC"
fi
if [[ "x$HAVE_IOS" = "xyes" ]]; then
  NPFRIDA_VALAFLAGS="$NPFRIDA_VALAFLAGS -D IOS"
fi
AC_SUBST(NPFRIDA_VALAFLAGS)

pkg_modules="frida-core-1.0 gee-0.8 json-glib-1.0"
NPFRIDA_PACKAGES="--pkg frida-core-1.0 --pkg gee-0.8 --pkg json-glib-1.0"
PKG_CHECK_MODULES(NPFRIDA, [$pkg_modules])
AC_SUBST(NPFRIDA_CFLAGS)
AC_SUBST(NPFRIDA_LDFLAGS)
AC_SUBST(NPFRIDA_LIBS)
AC_SUBST(NPFRIDA_PACKAGES)

AC_CONFIG_FILES([
  Makefile
  src/Makefile
])
AC_OUTPUT
