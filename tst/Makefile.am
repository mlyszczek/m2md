check_PROGRAMS = m2md_test
dist_check_SCRIPTS = m2md-progs.sh

m2md_test_source = main.c
m2md_test_header = mtest.h

m2md_test_SOURCES = $(m2md_test_source) $(m2md_test_header)
m2md_test_CFLAGS = -I$(top_srcdir)/inc \
	-I$(top_srcdir)/src \
	-I$(top_srcdir) \
	$(COVERAGE_CFLAGS)

m2md_test_LDFLAGS = $(COVERAGE_LDFLAGS) -static
m2md_test_LDADD = $(top_builddir)/src/libm2md.la

TESTS = $(check_PROGRAMS) $(dist_check_SCRIPTS)
LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/tap-driver.sh
EXTRA_DIST = mtest.sh
#CLEANFILES = m2md.log

# static code analyzer

if ENABLE_ANALYZER

analyze_plists = $(m2md_test_source:%.c=%.plist)
MOSTLYCLEANFILES = $(analyze_plists)

$(analyze_plists): %.plist: %.c
        @echo "  CCSA  " -cconfig.file
        @clang --analyze -DHAVE_CONFIG_H -I../inc -I../src -I.. -I. $< -o -cconfig.file

analyze: $(analyze_plists)

endif
