# Note: plugindir is set in configure

plugin_LTLIBRARIES = libgstyolo.la

# sources used to compile this plug-in
libgstyolo_la_SOURCES = gstyolo.c gstyolo.h

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstyolo_la_CFLAGS = $(GST_CFLAGS)
libgstyolo_la_LIBADD = $(GST_LIBS)
libgstyolo_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstyolo_la_LIBTOOLFLAGS = --tag=disable-static


# headers we need but don't want installed
noinst_HEADERS = gstyolo.h
