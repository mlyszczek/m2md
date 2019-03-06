AC_INIT([m2md], [0.1.0], [michal.lyszczek@bofc.pl])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_HEADERS([m2md-config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_PROG_CC
AC_PROG_LIBTOOL
LT_INIT
AC_CANONICAL_HOST
AC_CONFIG_FILES([Makefile src/Makefile tst/Makefile])
AC_CONFIG_LINKS([tst/mtest.sh:tst/mtest.sh])

m4_include([m4/gcov.m4])
AC_TDD_GCOV
AC_SUBST(COVERAGE_CFLAGS)
AC_SUBST(COVERAGE_CXXFLAGS)
AC_SUBST(COVERAGE_LDFLAGS)

AX_PTHREAD
LIBS="$PTHREAD_LIBS $LIBS"
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
CC="$PTHREAD_CC"

AC_SEARCH_LIBS([rb_new], [rb])
AC_SEARCH_LIBS([ini_parse], [inih])
AC_SEARCH_LIBS([el_init], [embedlog])
AC_SEARCH_LIBS([modbus_new_tcp], [modbus])
AC_SEARCH_LIBS([mosquitto_lib_init], [mosquitto])

AC_DEFINE([M2MD_CONFIG_PATH_DEFAULT], ["/etc/m2md.conf"],
    [default config file for program])


###
# --enable-analyzer
#

AC_ARG_ENABLE([analyzer],
    AS_HELP_STRING([--enable-analyzer], [Enable static code analyzer]),
    [enable_analyzer="yes"], [enable_analyzer="no"])
AM_CONDITIONAL([ENABLE_ANALYZER], [test "x$enable_analyzer" = "xyes"])


###
# --enable-standalone
#


AC_ARG_ENABLE([standalone],
    AS_HELP_STRING([--enable-standalone], [Create standalone binaries]),
    [], [enable_standalone="yes"])

AM_CONDITIONAL([ENABLE_STANDALONE], [test "x$enable_standalone" = "xyes"])


###
# --enable-library
#


AC_ARG_ENABLE([library],
    AS_HELP_STRING([--enable-library], [Create libraries from programs]),
    [], [enable_library="yes"])

AM_CONDITIONAL([ENABLE_LIBRARY], [test "x$enable_library" = "xyes"])

###
# --enable-ini
#


AC_ARG_ENABLE([ini],
    AS_HELP_STRING([--enable-ini], [Enable parsing ini config at startup]),
    [], [enable_ini="yes"])

AM_CONDITIONAL([ENABLE_INI], [test "x$enable_ini" = "xyes"])
AS_IF([test "x$enable_ini" = "xyes"],
[
    AC_DEFINE([M2MD_ENABLE_INI], [1], [Enable parsing ini config at startup])
],[])


###
# --enable-getopt
#


AC_ARG_ENABLE([getopt],
    AS_HELP_STRING([--enable-getopt], [Enable parsing getopt options at startup]),
    [], [enable_getopt="no"])

AM_CONDITIONAL([ENABLE_GETOPT], [test "x$enable_getopt" = "xyes"])
AS_IF([test "x$enable_getopt" = "xyes"],
[
    AC_DEFINE([M2MD_ENABLE_GETOPT], [1], [Enable parsing getopt options at startup])
],[])


###
# --enable-getopt_long
#


AC_ARG_ENABLE([getopt_long],
    AS_HELP_STRING([--enable-getopt_long], [Enable parsing getopt_long config at startup]),
    [], [enable_getopt_long="yes"])

AM_CONDITIONAL([ENABLE_GETOPT_LONG], [test "x$enable_getopt_long" = "xyes"])
AS_IF([test "x$enable_getopt_long" = "xyes"],
[
    AC_DEFINE([M2MD_ENABLE_GETOPT_LONG], [1], [Enable parsing getopt_long config at startup])
],[])

###
# VARIABLES=value options
#

###
# M2MD_SERVERS_MAX
#


AC_ARG_VAR([M2MD_SERVERS_MAX], [Maximum simultaneous servers handled])
AS_IF([test "x$M2MD_SERVERS_MAX" = x], [M2MD_SERVERS_MAX="128"])
AC_DEFINE_UNQUOTED([M2MD_SERVERS_MAX], [$M2MD_SERVERS_MAX],
           [Maximum simultaneous servers handled])

AS_IF([test $M2MD_SERVERS_MAX -lt 1],
[
    AC_MSG_ERROR(M2MD_SERVERS_MAX must be at least 1)
])


###
# M2MD_TOPIC_MAX
#


AC_ARG_VAR([M2MD_TOPIC_MAX], [Maximum length of mqtt topic])
AS_IF([test "x$M2MD_TOPIC_MAX" = x], [M2MD_TOPIC_MAX="4096"])
AC_DEFINE_UNQUOTED([M2MD_TOPIC_MAX], [$M2MD_TOPIC_MAX],
           [Maximum length of mqtt topic])


AC_OUTPUT

echo
echo "m2md compilation configuration summary"
echo
echo "build standalone.........: $enable_standalone"
echo "build library............: $enable_library"
echo "enable clang analyzer....: $enable_analyzer"
echo "enable gcov..............: $use_gcov"
echo
echo "enable ini config files..: $enable_ini"
echo "enable getopt args.......: $enable_getopt"
echo "enable getopt_long args..: $enable_getopt_long"
echo "max servers..............: $M2MD_SERVERS_MAX"
echo "max topic length.........: $M2MD_TOPIC_MAX"
