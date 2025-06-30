# sources and intermediate files are separated

AM_CFLAGS = -Wall \
    -Wundef \
    -Wstrict-prototypes \
    -Wno-trigraphs \
    -std=c99

AM_CPPFLAGS= \
    -Iinclude/

# HAL layer
c_sources = \
    src/ll.c \
    src/property_ops.c \
    src/sock_ops.c

library_includedir = $(pkgincludedir)
bin_PROGRAMS = btproperty
btproperty_CC = @CC@
btproperty_SOURCES = $(c_sources)
btproperty_CFLAGS = $(AM_CFLAGS)
btproperty_LDADD = -lpthread
