#include "npfrida-plugin.h"

#include "npfrida.h"
#include "npfrida-object.h"
#include "npfrida-object-priv.h"

#include <frida-core.h>
#ifdef G_OS_WIN32
# define VC_EXTRALEAN
# include <windows.h>
# include <tchar.h>
#endif
#include "npfunctions.h"

static gchar npfrida_mime_description[] = "application/x-vnd-frida:.frida:ole.andre.ravnas@tillitech.com";

static gint npfrida_get_process_id (void);

NPNetscapeFuncs * npfrida_nsfuncs = NULL;
GMainContext * npfrida_main_context = NULL;

G_LOCK_DEFINE_STATIC (npfrida_plugin);
static GHashTable * npfrida_plugin_roots = NULL;
static NPP npfrida_logging_instance = NULL;

static void
npfrida_log (const gchar * log_domain, GLogLevelFlags log_level, const gchar * message, gpointer user_data)
{
  NPNetscapeFuncs * browser = npfrida_nsfuncs;
  NPP instance = static_cast<NPP> (user_data);
  NPObject * window = NULL, * console = NULL;
  NPVariant variant, result;
  NPError error;

  (void) log_domain;
  (void) log_level;

  error = browser->getvalue (instance, NPNVWindowNPObject, &window);
  if (error != NPERR_NO_ERROR)
    goto beach;

  VOID_TO_NPVARIANT (variant);
  if (!browser->getproperty (instance, window, browser->getstringidentifier ("console"), &variant))
    goto beach;
  console = NPVARIANT_TO_OBJECT (variant);

  STRINGZ_TO_NPVARIANT (message, variant);
  VOID_TO_NPVARIANT (result);
  if (!browser->invoke (instance, console, browser->getstringidentifier ("log"), &variant, 1, &result))
    goto beach;
  browser->releasevariantvalue (&result);

beach:
  if (console != NULL)
    browser->releaseobject (console);
  if (window != NULL)
    browser->releaseobject (window);
}

static void
npfrida_startup (void)
{
  g_setenv ("G_DEBUG", "fatal-warnings:fatal-criticals", TRUE);
  frida_init ();
}

static void
npfrida_init_logging (NPP instance)
{
  if (npfrida_logging_instance != NULL)
    return;

  npfrida_logging_instance = instance;
  g_log_set_default_handler (npfrida_log, instance);
}

static void
npfrida_deinit_logging (NPP instance)
{
  if (instance != npfrida_logging_instance)
    return;

  npfrida_logging_instance = NULL;

  if (g_hash_table_size (npfrida_plugin_roots) == 0)
  {
    g_log_set_default_handler (g_log_default_handler, NULL);
  }
  else
  {
    GHashTableIter iter;
    NPP replacement_instance;

    g_hash_table_iter_init (&iter, npfrida_plugin_roots);
    g_hash_table_iter_next (&iter, reinterpret_cast<gpointer *> (&replacement_instance), NULL);
    npfrida_init_logging (replacement_instance);
  }
}

static NPError
npfrida_plugin_new (NPMIMEType plugin_type, NPP instance, uint16_t mode, int16_t argc, char * argn[], char * argv[],
    NPSavedData * saved)
{
  (void) plugin_type;
  (void) mode;
  (void) argc;
  (void) argn;
  (void) argv;
  (void) saved;

#ifdef HAVE_MAC
  NPBool supports_core_graphics;
  if (npfrida_nsfuncs->getvalue (instance, NPNVsupportsCoreGraphicsBool,
      &supports_core_graphics) == NPERR_NO_ERROR && supports_core_graphics)
  {
    npfrida_nsfuncs->setvalue (instance, NPPVpluginDrawingModel,
        reinterpret_cast<void *> (NPDrawingModelCoreGraphics));
  }

  NPBool supports_cocoa_events;
  if (npfrida_nsfuncs->getvalue (instance, NPNVsupportsCocoaBool,
      &supports_cocoa_events) == NPERR_NO_ERROR && supports_cocoa_events)
  {
    npfrida_nsfuncs->setvalue (instance, NPPVpluginEventModel,
        reinterpret_cast<void *> (NPEventModelCocoa));
  }
#endif

  npfrida_nsfuncs->setvalue (instance, NPPVpluginWindowBool, NULL);

  G_LOCK (npfrida_plugin);
  g_hash_table_insert (npfrida_plugin_roots, instance, NULL);
  npfrida_init_logging (instance);
  G_UNLOCK (npfrida_plugin);

  g_debug ("Frida plugin %p instantiated in pid %d", instance, npfrida_get_process_id ());

  return NPERR_NO_ERROR;
}

static NPError
npfrida_plugin_destroy (NPP instance, NPSavedData ** saved)
{
  NPFridaNPObject * root_object;

  (void) saved;

  G_LOCK (npfrida_plugin);
  root_object = static_cast<NPFridaNPObject *> (g_hash_table_lookup (npfrida_plugin_roots, instance));
  G_UNLOCK (npfrida_plugin);

  if (root_object != NULL)
    npfrida_np_object_destroy (root_object);

  G_LOCK (npfrida_plugin);
  g_hash_table_remove (npfrida_plugin_roots, instance);
  npfrida_deinit_logging (instance);
  G_UNLOCK (npfrida_plugin);

  g_debug ("Frida plugin %p destroyed in pid %d", instance, npfrida_get_process_id ());

  return NPERR_NO_ERROR;
}

static NPError
npfrida_plugin_set_window (NPP instance, NPWindow * window)
{
  (void) instance;
  (void) window;

  return NPERR_NO_ERROR;
}

static int16_t
npfrida_plugin_handle_event (NPP instance, void * ev)
{
  (void) instance;
  (void) ev;

  return NPERR_NO_ERROR;
}

static NPError
npfrida_plugin_get_value (NPP instance, NPPVariable variable, void * value)
{
  (void) instance;

  if (NP_GetValue (NULL, variable, value) == NPERR_NO_ERROR)
    return NPERR_NO_ERROR;

  switch (variable)
  {
    case NPPVpluginScriptableNPObject:
    {
      NPObject * obj;

      G_LOCK (npfrida_plugin);
      obj = static_cast<NPObject *> (g_hash_table_lookup (npfrida_plugin_roots, instance));
      if (obj == NULL)
      {
        obj = npfrida_nsfuncs->createobject (instance, static_cast<NPClass *> (npfrida_object_type_get_np_class (NPFRIDA_TYPE_ROOT)));
        g_hash_table_insert (npfrida_plugin_roots, instance, obj);
      }
      npfrida_nsfuncs->retainobject (obj);
      G_UNLOCK (npfrida_plugin);

      *(static_cast<NPObject **> (value)) = obj;
      break;
    }
    default:
      return NPERR_INVALID_PARAM;
  }

  return NPERR_NO_ERROR;
}

static void
npfrida_root_object_destroy (gpointer data)
{
  NPObject * obj = static_cast<NPObject *> (data);
  if (obj != NULL)
    npfrida_nsfuncs->releaseobject (obj);
}

char *
NP_GetMIMEDescription (void)
{
  return npfrida_mime_description;
}

NPError OSCALL
NP_GetValue (void * reserved, NPPVariable variable, void * value)
{
  (void) reserved;

  if (value == NULL)
    return NPERR_INVALID_PARAM;

  switch (variable)
  {
    case NPPVpluginNameString:
      *(static_cast<const char **> (value)) = "Frida";
      break;
    case NPPVpluginDescriptionString:
      *(static_cast<const char **> (value)) = "<a href=\"http://github.com/frida/\">Frida</a> plugin.";
      break;
    default:
      return NPERR_INVALID_PARAM;
  }

  return NPERR_NO_ERROR;
}

NPError OSCALL
NP_GetEntryPoints (NPPluginFuncs * pf)
{
  npfrida_startup ();

  pf->version = (NP_VERSION_MAJOR << 8) | NP_VERSION_MINOR;
  pf->newp = npfrida_plugin_new;
  pf->destroy = npfrida_plugin_destroy;
  pf->setwindow = npfrida_plugin_set_window;
  pf->event = npfrida_plugin_handle_event;
  pf->getvalue = npfrida_plugin_get_value;

  return NPERR_NO_ERROR;
}

NPError OSCALL
#ifdef HAVE_LINUX
NP_Initialize (NPNetscapeFuncs * nf, NPPluginFuncs * pf)
#else
NP_Initialize (NPNetscapeFuncs * nf)
#endif
{
  if (nf == NULL)
    return NPERR_INVALID_FUNCTABLE_ERROR;

#ifdef HAVE_LINUX
  NP_GetEntryPoints (pf);
#else
  npfrida_startup ();
#endif

  npfrida_object_type_init ();

  npfrida_nsfuncs = nf;
  npfrida_plugin_roots = g_hash_table_new_full (NULL, NULL, NULL, npfrida_root_object_destroy);

  npfrida_main_context = frida_get_main_context ();

  return NPERR_NO_ERROR;
}

NPError OSCALL
NP_Shutdown (void)
{
  frida_shutdown ();

  npfrida_main_context = NULL;

  g_hash_table_unref (npfrida_plugin_roots);
  npfrida_plugin_roots = NULL;
  npfrida_nsfuncs = NULL;

  npfrida_object_type_deinit ();

  frida_deinit ();

  return NPERR_NO_ERROR;
}

void
npfrida_init_npvariant_with_string (NPVariant * var, const gchar * str)
{
  gsize len = strlen (str);
  NPUTF8 * str_copy = static_cast<NPUTF8 *> (npfrida_nsfuncs->memalloc (len));
  memcpy (str_copy, str, len);
  STRINGN_TO_NPVARIANT (str_copy, len, *var);
}

gchar *
npfrida_npstring_to_cstring (const NPString * s)
{
  gchar * str;

  str = static_cast<gchar *> (g_malloc (s->UTF8Length + 1));
  memcpy (str, s->UTF8Characters, s->UTF8Length);
  str[s->UTF8Length] = '\0';

  return str;
}

void
npfrida_init_npvariant_with_other (NPVariant * var, const NPVariant * other)
{
  memcpy (var, other, sizeof (NPVariant));

  if (other->type == NPVariantType_String)
  {
    const NPString * from = &NPVARIANT_TO_STRING (*other);
    NPString * to = &NPVARIANT_TO_STRING (*var);
    to->UTF8Characters = static_cast<NPUTF8 *> (npfrida_nsfuncs->memalloc (from->UTF8Length));
    memcpy (const_cast<NPUTF8 *> (to->UTF8Characters), from->UTF8Characters, from->UTF8Length);
  }
  else if (other->type == NPVariantType_Object)
  {
    npfrida_nsfuncs->retainobject (NPVARIANT_TO_OBJECT (*var));
  }
}

void
npfrida_npobject_release (gpointer npobject)
{
  npfrida_nsfuncs->releaseobject (static_cast<NPObject *> (npobject));
}

static gint
npfrida_get_process_id (void)
{
#ifdef G_OS_WIN32
  return GetCurrentProcessId ();
#else
  return getpid ();
#endif
}
