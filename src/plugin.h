#ifndef __NPFRIDA_PLUGIN_H__
#define __NPFRIDA_PLUGIN_H__

#include <glib.h>
#ifdef G_OS_WIN32
# ifndef VC_EXTRALEAN
#  define VC_EXTRALEAN
# endif
# include <windows.h>
# include <tchar.h>
#endif
#include "npfunctions.h"

#define NPFRIDA_ATTACHPOINT() \
  MessageBox (NULL, _T (__FUNCTION__), _T (__FUNCTION__), MB_ICONINFORMATION | MB_OK)

G_BEGIN_DECLS

char * NP_GetMIMEDescription (void);
NPError OSCALL NP_GetValue (void * reserved, NPPVariable variable, void * value);
NPError OSCALL NP_GetEntryPoints (NPPluginFuncs * pf);
#ifdef HAVE_LINUX
NPError OSCALL NP_Initialize (NPNetscapeFuncs * nf, NPPluginFuncs * pf);
#else
NPError OSCALL NP_Initialize (NPNetscapeFuncs * nf);
#endif
NPError OSCALL NP_Shutdown (void);

G_GNUC_INTERNAL extern NPNetscapeFuncs * npfrida_nsfuncs;
G_GNUC_INTERNAL extern GMainContext * npfrida_main_context;

G_GNUC_INTERNAL void npfrida_init_npvariant_with_string (NPVariant * var, const gchar * str);
G_GNUC_INTERNAL gchar * npfrida_npstring_to_cstring (const NPString * s);
G_GNUC_INTERNAL void npfrida_init_npvariant_with_other (NPVariant * var, const NPVariant * other);
G_GNUC_INTERNAL void npfrida_npobject_release (gpointer npobject);

G_END_DECLS

#endif
