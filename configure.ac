
dnl required version of autoconf
AC_PREREQ([2.68])

AC_INIT([openwebrtc-gst-plugins],[0.10.0])

dnl required versions of gstreamer and plugins-base
GST_REQUIRED=1.0
GSTPB_REQUIRED=1.0

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

dnl required version of automake
AM_INIT_AUTOMAKE([1.10 foreign subdir-objects])

dnl enable mainainer mode by default
AM_MAINTAINER_MODE([enable])

dnl check for tools (compiler etc.)
AC_PROG_CC
AC_PROG_CXX
AC_PROG_OBJC
AM_PROG_AS

dnl required version of libtool
LT_PREREQ([2.2.6])
LT_INIT

AC_CANONICAL_HOST

dnl Check for the required version of GStreamer core (and gst-plugins-base)
dnl This will export GST_CFLAGS and GST_LIBS variables for use in Makefile.am
PKG_CHECK_MODULES(GST, [
  gstreamer-1.0 >= $GST_REQUIRED
  gstreamer-base-1.0 >= $GST_REQUIRED
  gstreamer-controller-1.0 >= $GST_REQUIRED
  gstreamer-video-1.0 >= $GST_REQUIRED
], [
  AC_SUBST(GST_CFLAGS)
  AC_SUBST(GST_OBJCFLAGS)
  AC_SUBST(GST_LIBS)
], [
  AC_MSG_ERROR([
      You need to install or upgrade the GStreamer development
      packages on your system. On debian-based systems these are
      libgstreamer1.0-dev and libgstreamer-plugins-base1.0-dev.
      on RPM-based systems gstreamer1-devel, libgstreamer1-devel
      or similar. The minimum version required is $GST_REQUIRED.
  ])
])

dnl Set target platform
AM_CONDITIONAL(TARGET_PLATFORM_IOS, test "x$host_alias" = "xarm-apple-darwin10")
AM_CONDITIONAL(TARGET_PLATFORM_ANDROID, test "x$host_alias" = "xarm-linux-androideabi")
AM_CONDITIONAL(TARGET_PLATFORM_LINUX, test "x$host_alias" = "xx86_64-unknown-linux")
AM_CONDITIONAL(TARGET_PLATFORM_OSX, test "x$host_alias" = "xx86_64-apple-darwin")
AM_CONDITIONAL(TARGET_PLATFORM_IOS_SIMULATOR, test "x$host_alias" = "xi386-apple-darwin10")

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

AC_CHECK_HEADERS([usrsctp.h],
  [AC_CHECK_LIB([usrsctp], [usrsctp_init],
    [with_sctp="yes"; USRSCTP_LIBS="-lusrsctp"],
    [with_sctp="no"]
  )],[with_sctp="no"]
)
AC_SUBST(USRSCTP_LIBS)
if test "x$with_sctp" != "xyes"; then
  AC_MSG_ERROR([You need libusrsctp for the build])
fi

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


dnl build android sys
AC_MSG_CHECKING([whether to build system plugins for android or not])
AC_ARG_ENABLE(
  android-plugins,
  AC_HELP_STRING(
    [--enable-android-plugins],
    [build android plugins @<:@default=no@:>@]),
  [AS_CASE(
    [$enableval], [no], [], [yes], [],
    [AC_MSG_ERROR([bad value "$enableval" for --enable-android-plugins])])],
  [enable_android_plugins=no])
AC_MSG_RESULT([$enable_android_plugins])
if test "x$enable_android_plugins" = xyes; then
  AC_DEFINE(BUILD_ANDROID_PLUGINS, 1,
    [Define if android plugins should be built])
  AC_PATH_PROG(DX, dx)
  if test -z "$DX"; then
    AC_MSG_ERROR([You need 'dx' from the Android SDK (not found in path $PATH and \$DX is not set)])
  fi
  AC_PATH_PROG(JAVAC, javac)
  if test -z "$JAVAC"; then
    AC_MSG_ERROR([You need 'javac' to build the Android plugins])
  fi
  AC_PATH_PROG(XXD, xxd)
  if test -z "$XXD"; then
    AC_MSG_ERROR([You need 'xxd' to build the Android plugins])
  fi

  api_found=0
  dx_path=`AS_DIRNAME(["$DX"])`
  AC_MSG_CHECKING([which Android API Level to use])
  for api_level in $(seq 9 22); do
    ANDROID_CLASSPATH="$dx_path/../../platforms/android-$api_level/android.jar"
    if test -f "$ANDROID_CLASSPATH"; then
      api_found=$api_level
      break
    fi
  done
  if test "$api_found" = 0; then
    AC_MSG_ERROR([No usable Android API levels not found in the SDK])
  fi
  AC_MSG_RESULT([$api_found])
  AC_SUBST(ANDROID_CLASSPATH)
fi
AM_CONDITIONAL(BUILD_ANDROID_PLUGINS, test "x$enable_android_plugins" = "xyes")
AM_COND_IF([BUILD_ANDROID_PLUGINS],
   AC_CONFIG_FILES([sys/androidvideo/Makefile sys/androidvideo/video-source/Makefile]))

dnl Check if osx plugins should be built
AC_MSG_CHECKING([whether to build system plugins for osx or not])
AC_ARG_ENABLE(
  osx-plugins,
  AC_HELP_STRING(
    [--enable-osx-plugins],
    [build osx plugins @<:@default=no@:>@]),
  [AS_CASE(
    [$enableval], [no], [], [yes], [],
    [AC_MSG_ERROR([bad value "$enableval" for --enable-osx-plugins])])],
  [enable_osx_plugins=no])
AC_MSG_RESULT([$enable_osx_plugins])
if test "x$enable_osx_plugins" = xyes; then
  AC_DEFINE(BUILD_OSX_PLUGINS, 1,
    [Define if osx plugins should be built])
fi
AM_CONDITIONAL(BUILD_OSX_PLUGINS, test "x$enable_osx_plugins" = "xyes")


dnl Check if linux plugins should be built
AC_MSG_CHECKING([whether to build system plugins for linux or not])
AC_ARG_ENABLE(
  linux-plugins,
  AC_HELP_STRING(
    [--enable-linux-plugins],
    [build linux plugins @<:@default=no@:>@]),
  [AS_CASE(
    [$enableval], [no], [], [yes], [],
    [AC_MSG_ERROR([bad value "$enableval" for --enable-linux-plugins])])],
  [enable_linux_plugins=no])
AC_MSG_RESULT([$enable_linux_plugins])
if test "x$enable_linux_plugins" = xyes; then
  AC_DEFINE(BUILD_LINUX_PLUGINS, 1,
    [Define if linux plugins should be built])
fi
AM_CONDITIONAL(BUILD_LINUX_PLUGINS, test "x$enable_linux_plugins" = "xyes")

dnl Release build
AC_MSG_CHECKING([whether to build debug or release])
AC_ARG_ENABLE(
  debug,
  AC_HELP_STRING(
    [--enable-debug],
    [build debug @<:@default=no@:>@]),
  [AS_CASE(
    [$enableval], [no], [], [yes], [],
    [AC_MSG_ERROR([bad value "$enableval" for --enable-debug])])],
  [enable_debug=no])
AC_MSG_RESULT([$enable_debug])
if test "x$enable_debug" = xyes; then
   AC_DEFINE(ENABLE_DEBUG, 1, [Define if debug should be built])
fi
AM_CONDITIONAL(ENABLE_DEBUG, test "x$enable_debug" = "xyes")

dnl Only build ercolorspace for android or ios builds.
AC_MSG_CHECKING([whether to build ercolorspace])
AC_ARG_ENABLE(
  colorspace-converter,
  AC_HELP_STRING(
    [--enable-colorspace-converter],
    [build colorspace converter @<:@default=no@:>@]),
  [AS_CASE(
    [$enableval], [no], [], [yes], [],
    [AC_MSG_ERROR([bad value "$enableval" for --enable-colorspace-converter])])],
  [enable_colorspace_converter=no])
AC_MSG_RESULT([$enable_colorspace_converter])
AM_CONDITIONAL(BUILD_ERCOLORSPACE, [ test "x$enable_colorspace_converter" = "xyes" ])
if test "x$enable_colorspace_converter" = "xyes"; then
    case $host in
        arm*)
          enable_colorspace_converter_neon="yes"
        ;;
        *)
          enable_colorspace_converter_neon="no"
        ;;
    esac
fi
AM_CONDITIONAL(BUILD_ERCOLORSPACE_NEON, [ test "x$enable_colorspace_converter_neon" = "xyes" ])
AM_COND_IF([BUILD_ERCOLORSPACE],
        AC_CONFIG_FILES([gst/ercolorspace/Makefile]))

dnl set the plugindir where plugins should be installed (for src/Makefile.am)
if test "x${prefix}" = "x$HOME"; then
  plugindir="$HOME/.gstreamer-1.0/plugins"
else
  plugindir="\$(libdir)/gstreamer-1.0"
fi
AC_SUBST(plugindir)

dnl set proper LDFLAGS for plugins
GST_PLUGIN_LDFLAGS='-module -avoid-version -export-symbols-regex [_]*\(gst_\|Gst\|GST_\).*'
AC_SUBST(GST_PLUGIN_LDFLAGS)

AC_CONFIG_FILES([Makefile gst-libs/Makefile gst-libs/gst/Makefile
                 gst-libs/gst/sctp/Makefile ext/Makefile
                 ext/sctp/Makefile gst/Makefile sys/Makefile
                 gst/scream/Makefile gst/videorepair/Makefile
                 gstreamer-sctp-1.0.pc gstreamer-sctp-1.0-uninstalled.pc])
AC_OUTPUT
