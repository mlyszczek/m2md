include Makefile.am.coverage
ACLOCAL_AMFLAGS=-I m4
SUBDIRS = src tst

if HAVE_GCOV
clean-local: clean-gcov
endif

EXTRA_DIST = tap-driver.sh readme.md

analyze:
	make analyze -C src
	make analyze -C lib
	make analyze -C tst

.PHONY: analyze
