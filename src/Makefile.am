#include ../Makefile.am.coverage

m2md_source = cfg.c main.c modbus.c mqtt.c poll-list.c reg2topic-map.c
m2md_headers = cfg.h $(top_srcdir)/valid.h

bin_cflags = $(COVERAGE_CFLAGS) -I$(top_srcdir) -I$(top_srcdir)/inc
bin_ldflags = $(COVERAGE_LDFLAGS)

if ENABLE_STANDALONE

bin_PROGRAMS = m2md
standalone_cflags = -DM2MD_STANDALONE=1

m2md_SOURCES = $(m2md_source) $(m2md_headers)
m2md_LDFLAGS = $(bin_ldflags)
m2md_CFLAGS = $(bin_cflags) $(standalone_cflags)

endif # ENABLE_STANDALONE

if ENABLE_LIBRARY

lib_LTLIBRARIES = libm2md.la
library_cflags = -DM2MD_LIBRARY=1

libm2md_la_SOURCES = $(m2md_source)
libm2md_la_CFLAGS = $(bin_cflags) $(library_cflags)
libm2md_la_LDFLAGS = $(bin_ldflags) -version-info 1:0:1

endif # ENABLE_LIBRARY

# static code analyzer

if ENABLE_ANALYZER

analyze_plists = $(m2md_source:%.c=%.plist)
MOSTLYCLEANFILES = $(analyze_plists)

$(analyze_plists): %.plist: %.c
	@echo "  CCSA  " -cconfig.file
	@clang --analyze -DHAVE_CONFIG_H -I../inc -I.. $< -o -cconfig.file

analyze: $(analyze_plists)

endif
