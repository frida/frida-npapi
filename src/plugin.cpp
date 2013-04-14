#include "cloud-spy-plugin.h"

#include "cloud-spy.h"
#include "cloud-spy-object.h"
#include "cloud-spy-object-priv.h"

#include <glib-object.h>
#ifdef G_OS_WIN32
# define VC_EXTRALEAN
# include <windows.h>
# include <tchar.h>
#endif
#include "npfunctions.h"

static gchar cloud_spy_mime_description[] = "application/x-vnd-cloud-spy:.cspy:ole.andre.ravnas@tillitech.com";

static gint cloud_spy_get_process_id (void);

NPNetscapeFuncs * cloud_spy_nsfuncs = NULL;
GMainContext * cloud_spy_main_context = NULL;
static GMainLoop * cloud_spy_main_loop = NULL;
static GThread * cloud_spy_main_thread = NULL;

G_LOCK_DEFINE_STATIC (cloud_spy_plugin);
static GHashTable * cloud_spy_plugin_roots = NULL;
static NPP cloud_spy_logging_instance = NULL;

static void
cloud_spy_log (const gchar * log_domain, GLogLevelFlags log_level, const gchar * message, gpointer user_data)
{
  NPNetscapeFuncs * browser = cloud_spy_nsfuncs;
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
cloud_spy_startup (void)
{
  g_setenv ("G_DEBUG", "fatal-warnings:fatal-criticals", TRUE);
  g_type_init ();
}

static void
cloud_spy_init_logging (NPP instance)
{
  if (cloud_spy_logging_instance != NULL)
    return;

  cloud_spy_logging_instance = instance;
  g_log_set_default_handler (cloud_spy_log, instance);
}

static void
cloud_spy_deinit_logging (NPP instance)
{
  if (instance != cloud_spy_logging_instance)
    return;

  cloud_spy_logging_instance = NULL;

  if (g_hash_table_size (cloud_spy_plugin_roots) == 0)
  {
    g_log_set_default_handler (g_log_default_handler, NULL);
  }
  else
  {
    GHashTableIter iter;
    NPP replacement_instance;

    g_hash_table_iter_init (&iter, cloud_spy_plugin_roots);
    g_hash_table_iter_next (&iter, reinterpret_cast<gpointer *> (&replacement_instance), NULL);
    cloud_spy_init_logging (replacement_instance);
  }
}

static gpointer
cloud_spy_run_main_loop (gpointer data)
{
  (void) data;

  g_main_context_push_thread_default (cloud_spy_main_context);
  g_main_loop_run (cloud_spy_main_loop);
  g_main_context_pop_thread_default (cloud_spy_main_context);

  return NULL;
}

static gboolean
cloud_spy_stop_main_loop (gpointer data)
{
  (void) data;

  g_main_loop_quit (cloud_spy_main_loop);

  return FALSE;
}

static NPError
cloud_spy_plugin_new (NPMIMEType plugin_type, NPP instance, uint16_t mode, int16_t argc, char * argn[], char * argv[],
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
  if (cloud_spy_nsfuncs->getvalue (instance, NPNVsupportsCoreGraphicsBool,
      &supports_core_graphics) == NPERR_NO_ERROR && supports_core_graphics)
  {
    cloud_spy_nsfuncs->setvalue (instance, NPPVpluginDrawingModel,
        reinterpret_cast<void *> (NPDrawingModelCoreGraphics));
  }

  NPBool supports_cocoa_events;
  if (cloud_spy_nsfuncs->getvalue (instance, NPNVsupportsCocoaBool,
      &supports_cocoa_events) == NPERR_NO_ERROR && supports_cocoa_events)
  {
    cloud_spy_nsfuncs->setvalue (instance, NPPVpluginEventModel,
        reinterpret_cast<void *> (NPEventModelCocoa));
  }
#endif

  cloud_spy_nsfuncs->setvalue (instance, NPPVpluginWindowBool, NULL);

  G_LOCK (cloud_spy_plugin);
  g_hash_table_insert (cloud_spy_plugin_roots, instance, NULL);
  cloud_spy_init_logging (instance);
  G_UNLOCK (cloud_spy_plugin);

  g_debug ("CloudSpy plugin %p instantiated in pid %d", instance, cloud_spy_get_process_id ());

  return NPERR_NO_ERROR;
}

static NPError
cloud_spy_plugin_destroy (NPP instance, NPSavedData ** saved)
{
  CloudSpyNPObject * root_object;

  (void) saved;

  G_LOCK (cloud_spy_plugin);
  root_object = static_cast<CloudSpyNPObject *> (g_hash_table_lookup (cloud_spy_plugin_roots, instance));
  G_UNLOCK (cloud_spy_plugin);

  if (root_object != NULL)
    cloud_spy_np_object_destroy (root_object);

  G_LOCK (cloud_spy_plugin);
  g_hash_table_remove (cloud_spy_plugin_roots, instance);
  cloud_spy_deinit_logging (instance);
  G_UNLOCK (cloud_spy_plugin);

  g_debug ("CloudSpy plugin %p destroyed in pid %d", instance, cloud_spy_get_process_id ());

  return NPERR_NO_ERROR;
}

static NPError
cloud_spy_plugin_set_window (NPP instance, NPWindow * window)
{
  (void) instance;
  (void) window;

  return NPERR_NO_ERROR;
}

static int16_t
cloud_spy_plugin_handle_event (NPP instance, void * ev)
{
  (void) instance;
  (void) ev;

  return NPERR_NO_ERROR;
}

static NPError
cloud_spy_plugin_get_value (NPP instance, NPPVariable variable, void * value)
{
  (void) instance;

  if (NP_GetValue (NULL, variable, value) == NPERR_NO_ERROR)
    return NPERR_NO_ERROR;

  switch (variable)
  {
    case NPPVpluginScriptableNPObject:
    {
      NPObject * obj;

      G_LOCK (cloud_spy_plugin);
      obj = static_cast<NPObject *> (g_hash_table_lookup (cloud_spy_plugin_roots, instance));
      if (obj == NULL)
      {
        obj = cloud_spy_nsfuncs->createobject (instance, static_cast<NPClass *> (cloud_spy_object_type_get_np_class (CLOUD_SPY_TYPE_ROOT)));
        g_hash_table_insert (cloud_spy_plugin_roots, instance, obj);
      }
      cloud_spy_nsfuncs->retainobject (obj);
      G_UNLOCK (cloud_spy_plugin);

      *(static_cast<NPObject **> (value)) = obj;
      break;
    }
    default:
      return NPERR_INVALID_PARAM;
  }

  return NPERR_NO_ERROR;
}

static void
cloud_spy_root_object_destroy (gpointer data)
{
  NPObject * obj = static_cast<NPObject *> (data);
  if (obj != NULL)
    cloud_spy_nsfuncs->releaseobject (obj);
}

char *
NP_GetMIMEDescription (void)
{
  return cloud_spy_mime_description;
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
      *(static_cast<const char **> (value)) = "CloudSpy";
      break;
    case NPPVpluginDescriptionString:
      *(static_cast<const char **> (value)) = "<a href=\"http://apps.facebook.com/cloud_spy/\">CloudSpy</a> plugin.";
      break;
    default:
      return NPERR_INVALID_PARAM;
  }

  return NPERR_NO_ERROR;
}

NPError OSCALL
NP_GetEntryPoints (NPPluginFuncs * pf)
{
  cloud_spy_startup ();

  pf->version = (NP_VERSION_MAJOR << 8) | NP_VERSION_MINOR;
  pf->newp = cloud_spy_plugin_new;
  pf->destroy = cloud_spy_plugin_destroy;
  pf->setwindow = cloud_spy_plugin_set_window;
  pf->event = cloud_spy_plugin_handle_event;
  pf->getvalue = cloud_spy_plugin_get_value;

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
  cloud_spy_startup ();
#endif

  cloud_spy_object_type_init ();

  cloud_spy_nsfuncs = nf;
  cloud_spy_plugin_roots = g_hash_table_new_full (NULL, NULL, NULL, cloud_spy_root_object_destroy);

  cloud_spy_main_context = g_main_context_new ();
  cloud_spy_main_loop = g_main_loop_new (cloud_spy_main_context, FALSE);
  cloud_spy_main_thread = g_thread_create (cloud_spy_run_main_loop, NULL, TRUE, NULL);

  return NPERR_NO_ERROR;
}

NPError OSCALL
NP_Shutdown (void)
{
  GSource * source;

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_LOW);
  g_source_set_callback (source, cloud_spy_stop_main_loop, NULL, NULL);
  g_source_attach (source, cloud_spy_main_context);
  g_source_unref (source);

  g_thread_join (cloud_spy_main_thread);
  cloud_spy_main_thread = NULL;
  g_main_loop_unref (cloud_spy_main_loop);
  cloud_spy_main_loop = NULL;
  g_main_context_unref (cloud_spy_main_context);
  cloud_spy_main_context = NULL;

  g_hash_table_unref (cloud_spy_plugin_roots);
  cloud_spy_plugin_roots = NULL;
  cloud_spy_nsfuncs = NULL;

  cloud_spy_object_type_deinit ();

  return NPERR_NO_ERROR;
}

void
cloud_spy_init_npvariant_with_string (NPVariant * var, const gchar * str)
{
  gsize len = strlen (str);
  NPUTF8 * str_copy = static_cast<NPUTF8 *> (cloud_spy_nsfuncs->memalloc (len));
  memcpy (str_copy, str, len);
  STRINGN_TO_NPVARIANT (str_copy, len, *var);
}

gchar *
cloud_spy_npstring_to_cstring (const NPString * s)
{
  gchar * str;

  str = static_cast<gchar *> (g_malloc (s->UTF8Length + 1));
  memcpy (str, s->UTF8Characters, s->UTF8Length);
  str[s->UTF8Length] = '\0';

  return str;
}

void
cloud_spy_init_npvariant_with_other (NPVariant * var, const NPVariant * other)
{
  memcpy (var, other, sizeof (NPVariant));

  if (other->type == NPVariantType_String)
  {
    const NPString * from = &NPVARIANT_TO_STRING (*other);
    NPString * to = &NPVARIANT_TO_STRING (*var);
    to->UTF8Characters = static_cast<NPUTF8 *> (cloud_spy_nsfuncs->memalloc (from->UTF8Length));
    memcpy (const_cast<NPUTF8 *> (to->UTF8Characters), from->UTF8Characters, from->UTF8Length);
  }
  else if (other->type == NPVariantType_Object)
  {
    cloud_spy_nsfuncs->retainobject (NPVARIANT_TO_OBJECT (*var));
  }
}

void
cloud_spy_npobject_release (gpointer npobject)
{
  cloud_spy_nsfuncs->releaseobject (static_cast<NPObject *> (npobject));
}

static gint
cloud_spy_get_process_id (void)
{
#ifdef G_OS_WIN32
  return GetCurrentProcessId ();
#else
  return getpid ();
#endif
}
