#include "cloud-spy-object.h"

#include "cloud-spy.h"
#include "cloud-spy-byte-array.h"
#include "cloud-spy-object-priv.h"
#include "cloud-spy-plugin.h"
#include "cloud-spy-promise.h"

typedef struct _CloudSpyObjectPrivate CloudSpyObjectPrivate;

typedef struct _CloudSpyDestroyContext CloudSpyDestroyContext;
typedef struct _CloudSpyInvokeContext CloudSpyInvokeContext;
typedef struct _CloudSpyGetPropertyContext CloudSpyGetPropertyContext;
typedef struct _CloudSpyClosure CloudSpyClosure;
typedef struct _CloudSpyClosureInvocation CloudSpyClosureInvocation;

struct _CloudSpyObjectPrivate
{
  NPP npp;
  CloudSpyDispatcher * dispatcher;
  GCond * cond;
  NPObject * json;
};

struct _CloudSpyDestroyContext
{
  CloudSpyObject * self;
  volatile gboolean completed;
};

struct _CloudSpyInvokeContext
{
  gchar * function_name;
  GVariant * arguments;
  NPObject * promise;
};

struct _CloudSpyGetPropertyContext
{
  CloudSpyObject * self;

  const gchar * property_name;
  GValue value;

  volatile gboolean completed;
};

struct _CloudSpyClosure
{
  GClosure closure;
  CloudSpyNPObject * object;
  NPObject * callback;
};

struct _CloudSpyClosureInvocation
{
  CloudSpyClosure * closure;
  GValueArray * args;
};

static void cloud_spy_object_constructed (GObject * object);
static void cloud_spy_object_dispose (GObject * object);
static void cloud_spy_object_finalize (GObject * object);

static gboolean cloud_spy_object_do_destroy (gpointer data);
static void cloud_spy_object_destroy_ready (GObject * source_object, GAsyncResult * res, gpointer user_data);
static gboolean cloud_spy_object_do_invoke (gpointer user_data);
static void cloud_spy_object_invoke_ready (GObject * source_object, GAsyncResult * res, gpointer user_data);
static gboolean cloud_spy_object_do_get_property (gpointer data);
static bool cloud_spy_object_add_event_listener (NPObject * npobj, const NPVariant * args, uint32_t arg_count, NPVariant * result);

static GVariant * cloud_spy_object_argument_list_to_gvariant (CloudSpyObject * self, const NPVariant * args, guint arg_count, GError ** err);
static void cloud_spy_object_return_value_to_npvariant (CloudSpyObject * self, GVariant * retval, NPVariant * result);
static void cloud_spy_object_gvariant_to_npvariant (CloudSpyObject * self, GVariant * retval, NPVariant * result);

static gboolean cloud_spy_object_gvalue_to_npvariant (CloudSpyObject * self, const GValue * gvalue, NPVariant * result);

static GClosure * cloud_spy_closure_new (CloudSpyNPObject * object, NPObject * callback);
static void cloud_spy_closure_finalize (gpointer data, GClosure * closure);
static void cloud_spy_closure_marshal (GClosure * closure, GValue * return_gvalue,
    guint n_param_values, const GValue * param_values, gpointer invocation_hint, gpointer marshal_data);
static void cloud_spy_closure_invoke (void * data);

G_DEFINE_TYPE (CloudSpyObject, cloud_spy_object, G_TYPE_OBJECT);

G_LOCK_DEFINE_STATIC (cloud_spy_object);

static void
cloud_spy_object_class_init (CloudSpyObjectClass * klass)
{
  GObjectClass * object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (CloudSpyObjectPrivate));

  object_class->constructed = cloud_spy_object_constructed;
  object_class->dispose = cloud_spy_object_dispose;
  object_class->finalize = cloud_spy_object_finalize;
}

static void
cloud_spy_object_init (CloudSpyObject * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, CLOUD_SPY_TYPE_OBJECT, CloudSpyObjectPrivate);

  self->priv->cond = g_cond_new ();
}

static void
cloud_spy_object_constructed (GObject * object)
{
  CloudSpyObject * self = CLOUD_SPY_OBJECT (object);

  self->priv->dispatcher = cloud_spy_dispatcher_new_for_object (self);

  if (G_OBJECT_CLASS (cloud_spy_object_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (cloud_spy_object_parent_class)->constructed (object);
}

static void
cloud_spy_object_dispose (GObject * object)
{
  CloudSpyObject * self = CLOUD_SPY_OBJECT (object);
  CloudSpyObjectPrivate * priv = self->priv;

  if (priv->dispatcher != NULL)
  {
    g_object_unref (priv->dispatcher);
    priv->dispatcher = NULL;
  }

  if (priv->json != NULL)
  {
    cloud_spy_nsfuncs->releaseobject (priv->json);
    priv->json = NULL;
  }

  G_OBJECT_CLASS (cloud_spy_object_parent_class)->dispose (object);
}

static void
cloud_spy_object_finalize (GObject * object)
{
  CloudSpyObject * self = CLOUD_SPY_OBJECT (object);

  g_cond_free (self->priv->cond);

  G_OBJECT_CLASS (cloud_spy_object_parent_class)->finalize (object);
}

void
cloud_spy_np_object_destroy (CloudSpyNPObject * obj)
{
  CloudSpyDestroyContext ctx = { 0, };
  GSource * source;

  ctx.self = obj->g_object;

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_LOW);
  g_source_set_callback (source, cloud_spy_object_do_destroy, &ctx, NULL);
  g_source_attach (source, cloud_spy_main_context);
  g_source_unref (source);

  G_LOCK (cloud_spy_object);
  while (!ctx.completed)
    g_cond_wait (ctx.self->priv->cond, g_static_mutex_get_mutex (&G_LOCK_NAME (cloud_spy_object)));
  G_UNLOCK (cloud_spy_object);
}

static gboolean
cloud_spy_object_do_destroy (gpointer data)
{
  CloudSpyDestroyContext * ctx = static_cast<CloudSpyDestroyContext *> (data);

  CLOUD_SPY_OBJECT_GET_CLASS (ctx->self)->destroy (ctx->self, cloud_spy_object_destroy_ready, ctx);

  return FALSE;
}

static void
cloud_spy_object_destroy_ready (GObject * source_object, GAsyncResult * res, gpointer user_data)
{
  CloudSpyDestroyContext * ctx = static_cast<CloudSpyDestroyContext *> (user_data);

  (void) source_object;

  CLOUD_SPY_OBJECT_GET_CLASS (ctx->self)->destroy_finish (ctx->self, res);

  G_LOCK (cloud_spy_object);
  ctx->completed = TRUE;
  g_cond_signal (ctx->self->priv->cond);
  G_UNLOCK (cloud_spy_object);
}

static NPObject *
cloud_spy_object_allocate (NPP npp, NPClass * klass)
{
  CloudSpyNPObjectClass * np_class = reinterpret_cast<CloudSpyNPObjectClass *> (klass);
  CloudSpyNPObject * obj;
  CloudSpyObjectPrivate * priv;
  NPNetscapeFuncs * browser = cloud_spy_nsfuncs;
  NPObject * window = NULL;
  NPVariant variant;
  NPError error;
  gboolean success;

  obj = g_slice_new (CloudSpyNPObject);
  obj->g_object = CLOUD_SPY_OBJECT (g_object_new (np_class->g_type, NULL));
  priv = obj->g_object->priv;

  priv->npp = npp;

  error = browser->getvalue (npp, NPNVWindowNPObject, &window);
  g_assert (error == NPERR_NO_ERROR);
  success = browser->getproperty (npp, window, browser->getstringidentifier ("JSON"), &variant);
  g_assert (success);
  priv->json = NPVARIANT_TO_OBJECT (variant);
  browser->releaseobject (window);

  return &obj->np_object;
}

static void
cloud_spy_object_deallocate (NPObject * npobj)
{
  CloudSpyNPObject * np_object = reinterpret_cast<CloudSpyNPObject *> (npobj);

  g_assert (np_object->g_object != NULL);
  g_object_unref (np_object->g_object);
  g_slice_free (CloudSpyNPObject, np_object);
}

static void
cloud_spy_object_invalidate (NPObject * npobj)
{
  (void) npobj;
}

static bool
cloud_spy_object_has_method (NPObject * npobj, NPIdentifier name)
{
  CloudSpyObjectPrivate * priv = reinterpret_cast<CloudSpyNPObject *> (npobj)->g_object->priv;
  const gchar * function_name;

  function_name = static_cast<NPString *> (name)->UTF8Characters;
  if (strcmp (function_name, "addEventListener") == 0)
    return true;

  return cloud_spy_dispatcher_has_method (priv->dispatcher, static_cast<NPString *> (name)->UTF8Characters) != FALSE;
}

static bool
cloud_spy_object_invoke (NPObject * npobj, NPIdentifier name, const NPVariant * args, uint32_t arg_count, NPVariant * result)
{
  const gchar * function_name;
  CloudSpyObject * self;
  GVariant * arguments = NULL;
  GError * error = NULL;
  CloudSpyInvokeContext * ctx;
  GSource * source;

  function_name = static_cast<NPString *> (name)->UTF8Characters;

  if (strcmp (function_name, "addEventListener") == 0)
  {
    return cloud_spy_object_add_event_listener (npobj, args, arg_count, result);
  }

  self = reinterpret_cast<CloudSpyNPObject *> (npobj)->g_object;

  arguments = cloud_spy_object_argument_list_to_gvariant (self, args, arg_count, &error);
  if (error != NULL)
    goto invoke_failed;

  cloud_spy_dispatcher_validate_invoke (self->priv->dispatcher, function_name, arguments, &error);
  if (error != NULL)
    goto invoke_failed;

  ctx = g_slice_new0 (CloudSpyInvokeContext);
  ctx->function_name = g_strdup (function_name);
  ctx->arguments = arguments;
  cloud_spy_nsfuncs->retainobject (npobj);
  ctx->promise = cloud_spy_promise_new (self->priv->npp, npobj, cloud_spy_npobject_release);

  cloud_spy_nsfuncs->retainobject (ctx->promise);
  OBJECT_TO_NPVARIANT (ctx->promise, *result);

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_HIGH);
  g_source_set_callback (source, cloud_spy_object_do_invoke, ctx, NULL);
  g_source_attach (source, cloud_spy_main_context);
  g_source_unref (source);

  return true;

invoke_failed:
  {
    if (arguments != NULL)
      g_variant_unref (arguments);
    cloud_spy_nsfuncs->setexception (npobj, error->message);
    g_clear_error (&error);
    return true;
  }
}

static gboolean
cloud_spy_object_do_invoke (gpointer user_data)
{
  CloudSpyInvokeContext * ctx = static_cast<CloudSpyInvokeContext *> (user_data);
  CloudSpyPromise * promise = reinterpret_cast<CloudSpyPromise *> (ctx->promise);
  CloudSpyObject * self = static_cast<CloudSpyNPObject *> (promise->user_data)->g_object;

  cloud_spy_dispatcher_invoke (self->priv->dispatcher, ctx->function_name, ctx->arguments,
      cloud_spy_object_invoke_ready, ctx);

  return FALSE;
}

static void
cloud_spy_object_invoke_ready (GObject * source_object, GAsyncResult * res, gpointer user_data)
{
  CloudSpyInvokeContext * ctx = static_cast<CloudSpyInvokeContext *> (user_data);
  CloudSpyPromise * promise = reinterpret_cast<CloudSpyPromise *> (ctx->promise);
  CloudSpyObject * self = static_cast<CloudSpyNPObject *> (promise->user_data)->g_object;
  GVariant * retval;
  GError * error = NULL;

  (void) source_object;

  retval = cloud_spy_dispatcher_invoke_finish (self->priv->dispatcher, res, &error);
  if (error == NULL)
  {
    if (retval == NULL)
    {
      cloud_spy_promise_resolve (promise, NULL, 0);
    }
    else
    {
      NPVariant val;
      cloud_spy_object_return_value_to_npvariant (self, retval, &val);
      cloud_spy_promise_resolve (promise, &val, 1);
      cloud_spy_nsfuncs->releasevariantvalue (&val);
    }

    if (retval != NULL)
      g_variant_unref (retval);
  }
  else
  {
    NPVariant message;

    STRINGZ_TO_NPVARIANT (error->message, message);
    cloud_spy_promise_reject (promise, &message, 1);
  }

  g_free (ctx->function_name);
  if (ctx->arguments != NULL)
    g_variant_unref (ctx->arguments);
  cloud_spy_nsfuncs->releaseobject (ctx->promise);
  g_slice_free (CloudSpyInvokeContext, ctx);
}

static bool
cloud_spy_object_invoke_default (NPObject * npobj, const NPVariant * args, uint32_t arg_count, NPVariant * result)
{
  (void) npobj;
  (void) args;
  (void) arg_count;
  (void) result;

  cloud_spy_nsfuncs->setexception (npobj, "invoke_default() is not yet implemented");
  return false;
}

static bool
cloud_spy_object_has_property (NPObject * npobj, NPIdentifier name)
{
  CloudSpyNPObjectClass * np_class = reinterpret_cast<CloudSpyNPObjectClass *> (npobj->_class);
  NPString * name_str = static_cast<NPString *> (name);

  return g_object_class_find_property (G_OBJECT_CLASS (np_class->g_class), name_str->UTF8Characters) != NULL;
}

static bool
cloud_spy_object_get_property (NPObject * npobj, NPIdentifier name, NPVariant * result)
{
  CloudSpyNPObject * np_object = reinterpret_cast<CloudSpyNPObject *> (npobj);
  CloudSpyNPObjectClass * np_class = reinterpret_cast<CloudSpyNPObjectClass *> (npobj->_class);
  CloudSpyGetPropertyContext ctx = { 0, };
  GParamSpec * spec;
  GSource * source;

  ctx.self = np_object->g_object;

  ctx.property_name = static_cast<NPString *> (name)->UTF8Characters;
  spec = g_object_class_find_property (G_OBJECT_CLASS (np_class->g_class), ctx.property_name);
  if (spec == NULL)
    goto no_such_property;
  g_value_init (&ctx.value, spec->value_type);

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_HIGH);
  g_source_set_callback (source, cloud_spy_object_do_get_property, &ctx, NULL);
  g_source_attach (source, cloud_spy_main_context);
  g_source_unref (source);

  G_LOCK (cloud_spy_object);
  while (!ctx.completed)
    g_cond_wait (ctx.self->priv->cond, g_static_mutex_get_mutex (&G_LOCK_NAME (cloud_spy_object)));
  G_UNLOCK (cloud_spy_object);

  if (!cloud_spy_object_gvalue_to_npvariant (ctx.self, &ctx.value, result))
    goto cannot_marshal;
  g_value_unset (&ctx.value);

  return true;

  /* ERRORS */
no_such_property:
  {
    cloud_spy_nsfuncs->setexception (npobj, "no such property");
    return false;
  }
cannot_marshal:
  {
    g_value_unset (&ctx.value);
    cloud_spy_nsfuncs->setexception (npobj, "type cannot be marshalled");
    return false;
  }
}

static gboolean
cloud_spy_object_do_get_property (gpointer data)
{
  CloudSpyGetPropertyContext * ctx = static_cast<CloudSpyGetPropertyContext *> (data);
  CloudSpyObjectPrivate * priv = ctx->self->priv;

  g_object_get_property (G_OBJECT (ctx->self), ctx->property_name, &ctx->value);

  G_LOCK (cloud_spy_object);
  ctx->completed = TRUE;
  g_cond_signal (priv->cond);
  G_UNLOCK (cloud_spy_object);

  return FALSE;
}

static bool
cloud_spy_object_add_event_listener (NPObject * npobj, const NPVariant * args, uint32_t arg_count, NPVariant * result)
{
  const NPVariant * signal_name, * signal_handler;
  gchar * signal_name_str;
  guint signal_id;

  if (arg_count != 2)
  {
    cloud_spy_nsfuncs->setexception (npobj, "addEventListener requires two arguments");
    return true;
  }

  signal_name = &args[0];
  if (signal_name->type != NPVariantType_String)
  {
    cloud_spy_nsfuncs->setexception (npobj, "event name must be a string");
    return true;
  }

  signal_handler = &args[1];
  if (signal_handler->type != NPVariantType_Object)
  {
    cloud_spy_nsfuncs->setexception (npobj, "event handler must be a function");
    return true;
  }

  signal_name_str = (gchar *) g_malloc (signal_name->value.stringValue.UTF8Length + 1);
  memcpy (signal_name_str, signal_name->value.stringValue.UTF8Characters, signal_name->value.stringValue.UTF8Length);
  signal_name_str[signal_name->value.stringValue.UTF8Length] = '\0';
  signal_id = g_signal_lookup (signal_name_str, G_OBJECT_TYPE (reinterpret_cast<CloudSpyNPObject *> (npobj)->g_object));
  g_free (signal_name_str), signal_name_str = NULL;

  if (signal_id == 0)
  {
    cloud_spy_nsfuncs->setexception (npobj, "invalid event name");
    return true;
  }

  g_signal_connect_closure_by_id (reinterpret_cast<CloudSpyNPObject *> (npobj)->g_object, signal_id, 0,
      cloud_spy_closure_new (reinterpret_cast<CloudSpyNPObject *> (npobj), signal_handler->value.objectValue), TRUE);

  VOID_TO_NPVARIANT (*result);
  return true;
}

static GVariant *
cloud_spy_object_argument_list_to_gvariant (CloudSpyObject * self, const NPVariant * args, guint arg_count, GError ** err)
{
  GVariantBuilder builder;
  guint i;

  g_variant_builder_init (&builder, G_VARIANT_TYPE_TUPLE);

  for (i = 0; i != arg_count; i++)
  {
    const NPVariant * var = &args[i];

    switch (var->type)
    {
      case NPVariantType_Bool:
        g_variant_builder_add_value (&builder, g_variant_new_boolean (NPVARIANT_TO_BOOLEAN (*var)));
        break;
      case NPVariantType_Int32:
        g_variant_builder_add_value (&builder, g_variant_new_int32 (NPVARIANT_TO_INT32 (*var)));
        break;
      case NPVariantType_Double:
        g_variant_builder_add_value (&builder, g_variant_new_double (NPVARIANT_TO_DOUBLE (*var)));
        break;
      case NPVariantType_String:
      {
        gchar * str;

        str = cloud_spy_npstring_to_cstring (&var->value.stringValue);
        g_variant_builder_add_value (&builder, g_variant_new_string (str));
        g_free (str);

        break;
      }
      case NPVariantType_Object:
      {
        NPVariant result;

        VOID_TO_NPVARIANT (result);
        if (cloud_spy_nsfuncs->invoke (self->priv->npp, self->priv->json, cloud_spy_nsfuncs->getstringidentifier ("stringify"), var, 1, &result))
        {
          gchar * str;

          str = cloud_spy_npstring_to_cstring (&result.value.stringValue);
          g_variant_builder_add_value (&builder, g_variant_new_string (str));
          g_free (str);

          cloud_spy_nsfuncs->releasevariantvalue (&result);

          break;
        }
      }
      case NPVariantType_Void:
      case NPVariantType_Null:
        goto invalid_type;
    }
  }

  return g_variant_builder_end (&builder);

invalid_type:
  {
    g_variant_builder_clear (&builder);
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "argument has invalid type");
    return NULL;
  }
}

static void
cloud_spy_object_return_value_to_npvariant (CloudSpyObject * self, GVariant * retval, NPVariant * result)
{
  if (retval == NULL)
  {
    VOID_TO_NPVARIANT (*result);
    return;
  }

  cloud_spy_object_gvariant_to_npvariant (self, retval, result);
}

static void
cloud_spy_object_gvariant_to_npvariant (CloudSpyObject * self, GVariant * retval, NPVariant * result)
{
  const GVariantType * type;

  type = g_variant_get_type (retval);
  if (g_variant_type_equal (type, G_VARIANT_TYPE_BOOLEAN))
  {
    BOOLEAN_TO_NPVARIANT (g_variant_get_boolean (retval), *result);
  }
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_INT32))
  {
    INT32_TO_NPVARIANT (g_variant_get_int32 (retval), *result);
  }
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_UINT32))
  {
    INT32_TO_NPVARIANT (g_variant_get_uint32 (retval), *result);
  }
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_DOUBLE))
  {
    DOUBLE_TO_NPVARIANT (g_variant_get_double (retval), *result);
  }
  else if (g_variant_type_equal (type, G_VARIANT_TYPE_STRING))
  {
    NPVariant variant;
    STRINGZ_TO_NPVARIANT (g_variant_get_string (retval, NULL), variant);
    VOID_TO_NPVARIANT (*result);
    cloud_spy_nsfuncs->invoke (self->priv->npp, self->priv->json, cloud_spy_nsfuncs->getstringidentifier ("parse"), &variant, 1, result);
  }
  else
  {
    g_assert_not_reached ();
  }
}

static gboolean
cloud_spy_object_gvalue_to_npvariant (CloudSpyObject * self, const GValue * gvalue, NPVariant * result)
{
  switch (G_VALUE_TYPE (gvalue))
  {
    case G_TYPE_BOOLEAN:
      BOOLEAN_TO_NPVARIANT (g_value_get_boolean (gvalue), *result);
      break;
    case G_TYPE_INT:
      INT32_TO_NPVARIANT (g_value_get_int (gvalue), *result);
      break;
    case G_TYPE_UINT:
      INT32_TO_NPVARIANT (g_value_get_uint (gvalue), *result);
      break;
    case G_TYPE_FLOAT:
      DOUBLE_TO_NPVARIANT ((double) g_value_get_float (gvalue), *result);
      break;
    case G_TYPE_DOUBLE:
      DOUBLE_TO_NPVARIANT (g_value_get_double (gvalue), *result);
      break;
    case G_TYPE_STRING:
      NPVariant variant;
      STRINGZ_TO_NPVARIANT (g_value_get_string (gvalue), variant);
      VOID_TO_NPVARIANT (*result);
      cloud_spy_nsfuncs->invoke (self->priv->npp, self->priv->json, cloud_spy_nsfuncs->getstringidentifier ("parse"), &variant, 1, result);
      break;
    case G_TYPE_VARIANT:
    {
      GVariant * variant;

      variant = g_value_get_variant (gvalue);
      if (variant == NULL)
      {
        NULL_TO_NPVARIANT (*result);
        break;
      }
      else if (g_variant_is_of_type (variant, G_VARIANT_TYPE ("ay")))
      {
        OBJECT_TO_NPVARIANT (cloud_spy_byte_array_new (self->priv->npp,
            static_cast<const guint8 *> (g_variant_get_data (variant)), g_variant_get_size (variant)), *result);
        break;
      }
    }
    default:
      VOID_TO_NPVARIANT (*result);
      return FALSE;
  }

  return TRUE;
}

static NPClass cloud_spy_object_template_np_class =
{
  NP_CLASS_STRUCT_VERSION,
  cloud_spy_object_allocate,
  cloud_spy_object_deallocate,
  cloud_spy_object_invalidate,
  cloud_spy_object_has_method,
  cloud_spy_object_invoke,
  cloud_spy_object_invoke_default,
  cloud_spy_object_has_property,
  cloud_spy_object_get_property,
  NULL,
  NULL,
  NULL,
  NULL
};

G_LOCK_DEFINE_STATIC (np_class_by_gobject_class);
static GHashTable * np_class_by_gobject_class = NULL;

void
cloud_spy_object_type_init (void)
{
  np_class_by_gobject_class = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_type_class_unref, g_free);
}

void
cloud_spy_object_type_deinit (void)
{
  if (np_class_by_gobject_class != NULL)
  {
    g_hash_table_unref (np_class_by_gobject_class);
    np_class_by_gobject_class = NULL;
  }
}

gpointer
cloud_spy_object_type_get_np_class (GType gtype)
{
  CloudSpyObjectClass * gobject_class;
  CloudSpyNPObjectClass * np_class;

  g_assert (g_type_is_a (gtype, CLOUD_SPY_TYPE_OBJECT));

  gobject_class = CLOUD_SPY_OBJECT_CLASS (g_type_class_ref (gtype));

  G_LOCK (np_class_by_gobject_class);

  np_class = static_cast<CloudSpyNPObjectClass *> (g_hash_table_lookup (np_class_by_gobject_class, gobject_class));
  if (np_class == NULL)
  {
    np_class = g_new (CloudSpyNPObjectClass, 1);

    memcpy (np_class, &cloud_spy_object_template_np_class, sizeof (cloud_spy_object_template_np_class));

    np_class->g_type = gtype;
    g_type_class_ref (gtype);
    np_class->g_class = gobject_class;

    g_hash_table_insert (np_class_by_gobject_class, np_class->g_class, np_class);
  }

  G_UNLOCK (np_class_by_gobject_class);

  g_type_class_unref (gobject_class);

  return np_class;
}

static GClosure *
cloud_spy_closure_new (CloudSpyNPObject * object, NPObject * callback)
{
  GClosure * closure;
  CloudSpyClosure * self;

  closure = g_closure_new_simple (sizeof (CloudSpyClosure), NULL);
  g_closure_add_finalize_notifier (closure, NULL, cloud_spy_closure_finalize);
  self = reinterpret_cast<CloudSpyClosure *> (closure);
  self->object = object;
  self->callback = cloud_spy_nsfuncs->retainobject (callback);

  g_closure_set_marshal (closure, cloud_spy_closure_marshal);

  return closure;
}

static void
cloud_spy_closure_finalize (gpointer data, GClosure * closure)
{
  CloudSpyClosure * self = reinterpret_cast<CloudSpyClosure *> (closure);

  (void) data;

  cloud_spy_nsfuncs->releaseobject (self->callback);
}

static void
cloud_spy_closure_marshal (GClosure * closure, GValue * return_gvalue, guint n_param_values, const GValue * param_values,
    gpointer invocation_hint, gpointer marshal_data)
{
  CloudSpyClosure * self = reinterpret_cast<CloudSpyClosure *> (closure);
  CloudSpyClosureInvocation * invocation;
  guint i;

  (void) return_gvalue;
  (void) invocation_hint;
  (void) marshal_data;

  invocation = g_slice_new (CloudSpyClosureInvocation);
  invocation->closure = self;
  invocation->args = g_value_array_new (n_param_values);
  for (i = 0; i != n_param_values; i++)
    g_value_array_append (invocation->args, &param_values[i]);

  cloud_spy_nsfuncs->pluginthreadasynccall (self->object->g_object->priv->npp, cloud_spy_closure_invoke, invocation);
}

static void
cloud_spy_closure_invoke (void * data)
{
  CloudSpyClosureInvocation * invocation = static_cast<CloudSpyClosureInvocation *> (data);
  CloudSpyClosure * self = invocation->closure;
  NPVariant * args;
  guint arg_count = invocation->args->n_values - 1;
  gboolean success = TRUE;
  guint i;

  args = static_cast<NPVariant *> (g_alloca (arg_count * sizeof (NPVariant)));
  for (i = 1; i != invocation->args->n_values; i++)
  {
    if (!cloud_spy_object_gvalue_to_npvariant (self->object->g_object, &invocation->args->values[i], &args[i - 1]))
    {
      success = FALSE;
      g_debug ("failed to convert argument %u to a variant", i - 1);
    }
  }

  if (success)
  {
    NPVariant result;

    VOID_TO_NPVARIANT (result);
    cloud_spy_nsfuncs->invokeDefault (self->object->g_object->priv->npp, self->callback, args, arg_count, &result);
    cloud_spy_nsfuncs->releasevariantvalue (&result);
  }

  for (i = 0; i != arg_count; i++)
    cloud_spy_nsfuncs->releasevariantvalue (&args[i]);

  g_value_array_free (invocation->args);
  g_slice_free (CloudSpyClosureInvocation, invocation);
}
