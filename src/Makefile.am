extra_ldflags =

if OS_LINUX
extra_ldflags += -Wl,--version-script -Wl,npfrida.version
else
extra_ldflags += -export-symbols-regex "^(NP_GetMIMEDescription|NP_GetValue|NP_GetEntryPoints|NP_Initialize|NP_Shutdown)$$"
endif

lib_LTLIBRARIES = libnpfrida-codegen.la libnpfrida.la

libnpfrida_codegen_la_SOURCES = \
	npfrida-api.vala \
	npfrida-root.vala \
	npfrida-codegen.c
libnpfrida_codegen_la_CFLAGS = \
	-w
libnpfrida_codegen_la_LDFLAGS = \
	-export-symbols-regex "^__hack_to_force_build_order__$$"
libnpfrida_codegen_la_VALAFLAGS = \
	--library=npfrida \
	--header=npfrida.h \
	--vapidir=$(abs_sourcedir) \
	--pkg=npfrida-object \
	@NPFRIDA_PACKAGES@ \
	@NPFRIDA_VALAFLAGS@

libnpfrida_la_SOURCES = \
	npfrida-plugin.h \
	npfrida-plugin.cpp \
	npfrida-object.h \
	npfrida-object-priv.h \
	npfrida-object.cpp \
	npfrida-promise.h \
	npfrida-promise.cpp \
	npfrida-byte-array.h \
	npfrida-byte-array.cpp \
	npfrida-api-glue.c \
	npfrida-root.c
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
