extra_ldflags =

if OS_LINUX
extra_ldflags += -Wl,--version-script -Wl,npfrida.version
else
extra_ldflags += -export-symbols-regex "^(NP_GetMIMEDescription|NP_GetValue|NP_GetEntryPoints|NP_Initialize|NP_Shutdown)$$"
endif

lib_LTLIBRARIES = libnpfrida-codegen.la libnpfrida.la

libnpfrida_codegen_la_SOURCES = \
	api.vala \
	root.vala \
	codegen.c
libnpfrida_codegen_la_CFLAGS = \
	-w
libnpfrida_codegen_la_LDFLAGS = \
	-export-symbols-regex "^__hack_to_force_build_order__$$"
libnpfrida_codegen_la_VALAFLAGS = \
	--library=npfrida \
	--header=npfrida.h \
	--vapidir=$(abs_sourcedir) \
	--pkg=npfrida-object \
	--pkg=json-glib-1.0 \
	--pkg=frida-core \
	@NPFRIDA_VALAFLAGS@

libnpfrida_la_SOURCES = \
	plugin.h \
	plugin.cpp \
	object.h \
	object-priv.h \
	object.cpp \
	promise.h \
	promise.cpp \
	byte-array.h \
	byte-array.cpp \
	api-glue.c \
	root.c
libnpfrida_la_LDFLAGS = \
	$(NPFRIDA_LDFLAGS) \
	$(extra_ldflags)
libnpfrida_la_LIBADD = \
	$(NPFRIDA_LIBS)
libnpfrida_la_LIBTOOLFLAGS = \
	--tag=disable-static

AM_CPPFLAGS = \
	-include config.h \
	$(NPFRIDA_CFLAGS)
