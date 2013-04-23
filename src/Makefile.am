extra_ldflags =

if OS_LINUX
extra_ldflags += -Wl,--version-script -Wl,$(srcdir)/npfrida.version
else
extra_ldflags += -export-symbols-regex "^(NP_GetMIMEDescription|NP_GetValue|NP_GetEntryPoints|NP_Initialize|NP_Shutdown)$$"
endif

plugindir = $(libdir)/browser/plugins
plugin_LTLIBRARIES = libnpfrida.la

noinst_LTLIBRARIES = \
	libnpfrida-codegen.la \
	libnpfrida-handwritten.la \
	libnpfrida-generated.la

libnpfrida_la_SOURCES = \
	$(NULL)
libnpfrida_la_LDFLAGS = \
	-avoid-version -shared \
	$(NPFRIDA_LDFLAGS) \
	$(extra_ldflags)
libnpfrida_la_LIBADD = \
	libnpfrida-handwritten.la \
	libnpfrida-generated.la \
	$(NPFRIDA_LIBS)
libnpfrida_la_LIBTOOLFLAGS = \
	--tag=disable-static

libnpfrida_handwritten_la_SOURCES = \
	npfrida-plugin.h \
	npfrida-plugin.cpp \
	npfrida-object.h \
	npfrida-object-priv.h \
	npfrida-object.cpp \
	npfrida-promise.h \
	npfrida-promise.cpp \
	npfrida-byte-array.h \
	npfrida-byte-array.cpp \
	npfrida-api-glue.c

libnpfrida_generated_la_SOURCES = \
	npfrida-root.c
libnpfrida_generated_la_CFLAGS = \
	-w

libnpfrida_codegen_la_SOURCES = \
	npfrida-api.vala \
	npfrida-root.vala \
	npfrida-codegen.c
libnpfrida_codegen_la_CFLAGS = \
	-w
libnpfrida_codegen_la_LDFLAGS = \
	-export-symbols-regex "^__hack_to_force_build_order__$$" \
	$(NPFRIDA_LDFLAGS) \
libnpfrida_codegen_la_LIBADD = \
	$(NPFRIDA_LIBS)
libnpfrida_codegen_la_VALAFLAGS = \
	--library=npfrida \
	--header=npfrida.h \
	--vapidir=$(abs_sourcedir) \
	--pkg=npfrida-object \
	@NPFRIDA_PACKAGES@ \
	@NPFRIDA_VALAFLAGS@

AM_CPPFLAGS = \
	-include config.h \
	$(NPFRIDA_CFLAGS)
