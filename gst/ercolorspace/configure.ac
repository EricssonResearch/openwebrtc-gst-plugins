dnl required version of autoconf
AC_PREREQ([2.53])

dnl TODO: fill in your package name and package version here
AC_INIT([my-plugin-package],[0.10.0])

dnl required versions of gstreamer and plugins-base
GST_REQUIRED=0.10.16
GSTPB_REQUIRED=0.10.16

AC_CONFIG_SRCDIR([src/gstercolorspace.c])
AC_CONFIG_HEADERS([config.h])

dnl required version of automake AM_INIT_AUTOMAKE([1.10])
AM_INIT_AUTOMAKE([1.10])

dnl enable mainainer mode by default
AM_MAINTAINER_MODE([enable])

dnl check for tools (compiler etc.)
AC_PROG_CC
AM_PROG_AS

dnl required version of libtool
LT_PREREQ([2.2.6])
LT_INIT

dnl give error and exit if we don't have pkgconfig
AC_CHECK_PROG(HAVE_PKGCONFIG, pkg-config, [ ], [
  AC_MSG_ERROR([You need to have pkg-config installed!])
])

dnl Check for the required version of GStreamer core (and gst-plugins-base)
dnl This will export GST_CFLAGS and GST_LIBS variables for use in Makefile.am
dnl
dnl If you need libraries from gst-plugins-base here, also add:
dnl for libgstaudio-0.10: gstreamer-audio-0.10 >= $GST_REQUIRED
dnl for libgstvideo-0.10: gstreamer-video-0.10 >= $GST_REQUIRED
dnl for libgsttag-0.10: gstreamer-tag-0.10 >= $GST_REQUIRED
dnl for libgstpbutils-0.10: gstreamer-pbutils-0.10 >= $GST_REQUIRED
dnl for libgstfft-0.10: gstreamer-fft-0.10 >= $GST_REQUIRED
dnl for libgstinterfaces-0.10: gstreamer-interfaces-0.10 >= $GST_REQUIRED
dnl for libgstrtp-0.10: gstreamer-rtp-0.10 >= $GST_REQUIRED
dnl for libgstrtsp-0.10: gstreamer-rtsp-0.10 >= $GST_REQUIRED
dnl etc.
PKG_CHECK_MODULES(GST, [
  gstreamer-0.10 >= $GST_REQUIRED
  gstreamer-base-0.10 >= $GST_REQUIRED
  gstreamer-controller-0.10 >= $GST_REQUIRED
], [
  AC_SUBST(GST_CFLAGS)
  AC_SUBST(GST_LIBS)
], [
  AC_MSG_ERROR([
      You need to install or upgrade the GStreamer development
      packages on your system. On debian-based systems these are
      libgstreamer0.10-dev and libgstreamer-plugins-base0.10-dev.
      on RPM-based systems gstreamer0.10-devel, libgstreamer0.10-devel
      or similar. The minimum version required is $GST_REQUIRED.
  ])
])

dnl check if compiler understands -Wall (if yes, add -Wall to GST_CFLAGS)
AC_MSG_CHECKING([to see if compiler understands -Wall])
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([ ], [ ])], [
  GST_CFLAGS="$GST_CFLAGS -Wall"
  AC_MSG_RESULT([yes])
], [
  AC_MSG_RESULT([no])
])

dnl set the plugindir where plugins should be installed (for src/Makefile.am)
if test "x${prefix}" = "x$HOME"; then
  plugindir="$HOME/.gstreamer-0.10/plugins"
else
  plugindir="\$(libdir)/gstreamer-0.10"
fi
AC_SUBST(plugindir)

dnl set proper LDFLAGS for plugins
GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_.*' $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS)


dnl build static plugins or not
AC_MSG_CHECKING([whether to build static plugins or not])
AC_ARG_ENABLE(
  static-plugins,
  AC_HELP_STRING(
    [--enable-static-plugins],
    [build static plugins @<:@default=no@:>@]),
  [AS_CASE(
    [$enableval], [no], [], [yes], [],
    [AC_MSG_ERROR([bad value "$enableval" for --enable-static-plugins])])],
    [enable_static_plugins=no])
  AC_MSG_RESULT([$enable_static_plugins])
if test "x$enable_static_plugins" = xyes; then
  AC_DEFINE(GST_PLUGIN_BUILD_STATIC, 1,
  [Define if static plugins should be built])
fi
AM_CONDITIONAL(GST_PLUGIN_BUILD_STATIC, test "x$enable_static_plugins" = "xyes")


dnl AC_CONFIG_FILES([Makefile src/Makefile])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT