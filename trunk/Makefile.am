
## Makefile.am for the smjpeg library

# These are the subdirectories that are always built
SUBDIRS = adpcm jpeg-6b

# The smjpeg library target
lib_LTLIBRARIES = libsmjpeg.la

libsmjpeg_la_SOURCES =		\
	smjpeg_decode.c

libsmjpegincludedir = $(includedir)/smjpeg
libsmjpeginclude_HEADERS =	\
	smjpeg_decode.h		\
	smjpeg_file.h

libsmjpeg_la_LDFLAGS = 		\
        -release $(LT_RELEASE)	\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)

libsmjpeg_la_LIBADD =		\
	adpcm/libadpcm.la	\
	jpeg-6b/libjpeg.la

EXTRA_DIST =		\
	CHANGES		\
	COPYING		\
	README		\
	SMJPEG.txt	\
	TODO

bin_PROGRAMS = smjpeg_encode smjpeg_decode

# Sources for smjpeg_encode
smjpeg_encode_SOURCES = smjpeg_encode.c
smjpeg_encode_LDADD = libsmjpeg.la

# Sources for smjpeg_decode
smjpeg_decode_SOURCES = play_smjpeg.c
smjpeg_decode_LDADD = libsmjpeg.la

# Rule to build tar-gzipped distribution package
$(PACKAGE)-$(VERSION).tar.gz: dist

# Rule to build RPM distribution package
rpm: $(PACKAGE)-$(VERSION).tar.gz
	cp $(PACKAGE)-$(VERSION).tar.gz /usr/src/redhat/SOURCES
	rpm -ba smjpeg.spec

