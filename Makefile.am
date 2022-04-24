include Makefile.am.coverage
ACLOCAL_AMFLAGS=-I m4
SUBDIRS = src tst

confdir = $(sysconfdir)/m2md
dist_conf_DATA= cfg/poll-list.conf cfg/m2md.ini

if HAVE_GCOV
clean-local: clean-gcov
endif

EXTRA_DIST = tap-driver.sh

analyze:
	make analyze -C src
	make analyze -C lib
	make analyze -C tst

.PHONY: analyze
