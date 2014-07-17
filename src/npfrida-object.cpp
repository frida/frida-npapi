#include "npfrida-object.h"

#include "npfrida.h"
#include "npfrida-byte-array.h"
#include "npfrida-object-priv.h"
#include "npfrida-plugin.h"
#include "npfrida-promise.h"

typedef struct _NPFridaObjectPrivate NPFridaObjectPrivate;

typedef struct _NPFridaDestroyContext NPFridaDestroyContext;
typedef struct _NPFridaInvokeContext NPFridaInvokeContext;
typedef struct _NPFridaGetPropertyContext NPFridaGetPropertyContext;
typedef struct _NPFridaClosure NPFridaClosure;
typedef struct _NPFridaClosureInvocation NPFridaClosureInvocation;

struct _NPFridaObjectPrivate
{
  NPP npp;
  NPFridaDispatcher * dispatcher;
  GCond cond;
  NPObject * json;
};

struct _NPFridaDestroyContext
{
  NPFridaObject * self;
  volatile gboolean completed;
};

struct _NPFridaInvokeContext
{
  gchar * function_name;
  GVariant * arguments;
  NPObject * promise;
  GVariant * retval;
  GError * error;
};

struct _NPFridaGetPropertyContext
{
  NPFridaObject * self;

  const gchar * property_name;
  GValue value;

  volatile gboolean completed;
};

struct _NPFridaClosure
{
  GClosure closure;
  NPFridaNPObject * object;
  NPObject * callback;
};

struct _NPFridaClosureInvocation
{
  NPFridaClosure * closure;
  GArray * args;
};

static void npfrida_object_constructed (GObject * object);
static void npfrida_object_dispose (GObject * object);
static void npfrida_object_finalize (GObject * object);

static gboolean npfrida_object_do_destroy (gpointer data);
static void npfrida_object_destroy_ready (GObject * source_object, GAsyncResult * res, gpointer user_data);
static gboolean npfrida_object_begin_invoke (gpointer user_data);
static void npfrida_object_on_invoke_ready (GObject * source_object, GAsyncResult * res, gpointer user_data);
static void npfrida_object_end_invoke (void * data);
static gboolean npfrida_object_do_get_property (gpointer data);
static bool npfrida_object_add_event_listener (NPObject * npobj, const NPVariant * args, uint32_t arg_count, NPVariant * result);

static GVariant * npfrida_object_argument_list_to_gvariant (NPFridaObject * self, const NPVariant * args, guint arg_count, GError ** err);
static void npfrida_object_return_value_to_npvariant (NPFridaObject * self, GVariant * retval, NPVariant * result);
static void npfrida_object_gvariant_to_npvariant (NPFridaObject * self, GVariant * retval, NPVariant * result);

static gboolean npfrida_object_gvalue_to_npvariant (NPFridaObject * self, const GValue * gvalue, NPVariant * result);

static GClosure * npfrida_closure_new (NPFridaNPObject * object, NPObject * callback);
static void npfrida_closure_finalize (gpointer data, GClosure * closure);
static void npfrida_closure_marshal (GClosure * closure, GValue * return_gvalue,
    guint n_param_values, const GValue * param_values, gpointer invocation_hint, gpointer marshal_data);
static void npfrida_closure_invoke (void * data);

G_DEFINE_TYPE (NPFridaObject, npfrida_object, G_TYPE_OBJECT);

G_LOCK_DEFINE_STATIC (npfrida_object);

static void
npfrida_object_class_init (NPFridaObjectClass * klass)
{
  GObjectClass * object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (NPFridaObjectPrivate));

  object_class->constructed = npfrida_object_constructed;
  object_class->dispose = npfrida_object_dispose;
  object_class->finalize = npfrida_object_finalize;
}

static void
npfrida_object_init (NPFridaObject * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, NPFRIDA_TYPE_OBJECT, NPFridaObjectPrivate);

  g_cond_init (&self->priv->cond);
}

static void
npfrida_object_constructed (GObject * object)
{
  NPFridaObject * self = NPFRIDA_OBJECT (object);

  self->priv->dispatcher = npfrida_dispatcher_new_for_object (self);

  if (G_OBJECT_CLASS (npfrida_object_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (npfrida_object_parent_class)->constructed (object);
}

static void
npfrida_object_dispose (GObject * object)
{
  NPFridaObject * self = NPFRIDA_OBJECT (object);
  NPFridaObjectPrivate * priv = self->priv;

  if (priv->dispatcher != NULL)
  {
    g_object_unref (priv->dispatcher);
    priv->dispatcher = NULL;
  }

  if (priv->json != NULL)
  {
    npfrida_nsfuncs->releaseobject (priv->json);
    priv->json = NULL;
  }

  G_OBJECT_CLASS (npfrida_object_parent_class)->dispose (object);
}

static void
npfrida_object_finalize (GObject * object)
{
  NPFridaObject * self = NPFRIDA_OBJECT (object);

  g_cond_clear (&self->priv->cond);

  G_OBJECT_CLASS (npfrida_object_parent_class)->finalize (object);
}

void
npfrida_np_object_destroy (NPFridaNPObject * obj)
{
  NPFridaDestroyContext ctx = { 0, };
  GSource * source;

  ctx.self = obj->g_object;

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_LOW);
  g_source_set_callback (source, npfrida_object_do_destroy, &ctx, NULL);
  g_source_attach (source, npfrida_main_context);
  g_source_unref (source);

  G_LOCK (npfrida_object);
  while (!ctx.completed)
    g_cond_wait (&ctx.self->priv->cond, &G_LOCK_NAME (npfrida_object));
  G_UNLOCK (npfrida_object);
}

static gboolean
npfrida_object_do_destroy (gpointer data)
{
  NPFridaDestroyContext * ctx = static_cast<NPFridaDestroyContext *> (data);

  NPFRIDA_OBJECT_GET_CLASS (ctx->self)->destroy (ctx->self, npfrida_object_destroy_ready, ctx);

  return FALSE;
}

static void
npfrida_object_destroy_ready (GObject * source_object, GAsyncResult * res, gpointer user_data)
{
  NPFridaDestroyContext * ctx = static_cast<NPFridaDestroyContext *> (user_data);

  (void) source_object;

  NPFRIDA_OBJECT_GET_CLASS (ctx->self)->destroy_finish (ctx->self, res);

  G_LOCK (npfrida_object);
  ctx->completed = TRUE;
  g_cond_signal (&ctx->self->priv->cond);
  G_UNLOCK (npfrida_object);
}

static NPObject *
npfrida_object_allocate (NPP npp, NPClass * klass)
{
  NPFridaNPObjectClass * np_class = reinterpret_cast<NPFridaNPObjectClass *> (klass);
  NPFridaNPObject * obj;
  NPFridaObjectPrivate * priv;
  NPNetscapeFuncs * browser = npfrida_nsfuncs;
  NPObject * window = NULL;
  NPVariant variant;
  NPError error;
  gboolean success;

  obj = g_slice_new (NPFridaNPObject);
  obj->g_object = NPFRIDA_OBJECT (g_object_new (np_class->g_type, NULL));
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
npfrida_object_deallocate (NPObject * npobj)
{
  NPFridaNPObject * np_object = reinterpret_cast<NPFridaNPObject *> (npobj);

  g_assert (np_object->g_object != NULL);
  g_object_unref (np_object->g_object);
  g_slice_free (NPFridaNPObject, np_object);
}

static void
npfrida_object_invalidate (NPObject * npobj)
{
  (void) npobj;
}

static bool
npfrida_object_has_method (NPObject * npobj, NPIdentifier name)
{
  NPFridaObjectPrivate * priv = reinterpret_cast<NPFridaNPObject *> (npobj)->g_object->priv;
  const gchar * function_name;

  function_name = static_cast<NPString *> (name)->UTF8Characters;
  if (strcmp (function_name, "addEventListener") == 0)
    return true;

  return npfrida_dispatcher_has_method (priv->dispatcher, static_cast<NPString *> (name)->UTF8Characters) != FALSE;
}

static bool
npfrida_object_invoke (NPObject * npobj, NPIdentifier name, const NPVariant * args, uint32_t arg_count, NPVariant * result)
{
  const gchar * function_name;
  NPFridaObject * self;
  GVariant * arguments = NULL;
  GError * error = NULL;
  NPFridaInvokeContext * ctx;
  GSource * source;

  function_name = static_cast<NPString *> (name)->UTF8Characters;

  if (strcmp (function_name, "addEventListener") == 0)
  {
    return npfrida_object_add_event_listener (npobj, args, arg_count, result);
  }

  self = reinterpret_cast<NPFridaNPObject *> (npobj)->g_object;

  arguments = npfrida_object_argument_list_to_gvariant (self, args, arg_count, &error);
  if (error != NULL)
    goto invoke_failed;

  npfrida_dispatcher_validate_invoke (self->priv->dispatcher, function_name, arguments, &error);
  if (error != NULL)
    goto invoke_failed;

  ctx = g_slice_new0 (NPFridaInvokeContext);
  ctx->function_name = g_strdup (function_name);
  ctx->arguments = arguments;
  npfrida_nsfuncs->retainobject (npobj);
  ctx->promise = npfrida_promise_new (self->priv->npp, npobj, npfrida_npobject_release);

  npfrida_nsfuncs->retainobject (ctx->promise);
  OBJECT_TO_NPVARIANT (ctx->promise, *result);

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_HIGH);
  g_source_set_callback (source, npfrida_object_begin_invoke, ctx, NULL);
  g_source_attach (source, npfrida_main_context);
  g_source_unref (source);

  return true;

invoke_failed:
  {
    if (arguments != NULL)
      g_variant_unref (arguments);
    npfrida_nsfuncs->setexception (npobj, error->message);
    g_clear_error (&error);
    return true;
  }
}

static gboolean
npfrida_object_begin_invoke (gpointer user_data)
{
  NPFridaInvokeContext * ctx = static_cast<NPFridaInvokeContext *> (user_data);
  NPFridaPromise * promise = reinterpret_cast<NPFridaPromise *> (ctx->promise);
  NPFridaObject * self = static_cast<NPFridaNPObject *> (promise->user_data)->g_object;

  npfrida_dispatcher_invoke (self->priv->dispatcher, ctx->function_name, ctx->arguments,
      npfrida_object_on_invoke_ready, ctx);

  return FALSE;
}

static void
npfrida_object_on_invoke_ready (GObject * source_object, GAsyncResult * res, gpointer user_data)
{
  NPFridaInvokeContext * ctx = static_cast<NPFridaInvokeContext *> (user_data);
  NPFridaPromise * promise = reinterpret_cast<NPFridaPromise *> (ctx->promise);
  NPFridaObject * self = static_cast<NPFridaNPObject *> (promise->user_data)->g_object;

  (void) source_object;

  ctx->retval = npfrida_dispatcher_invoke_finish (self->priv->dispatcher, res, &ctx->error);
  npfrida_nsfuncs->pluginthreadasynccall (self->priv->npp, npfrida_object_end_invoke, ctx);
}

static void
npfrida_object_end_invoke (void * data)
{
  NPFridaInvokeContext * ctx = static_cast<NPFridaInvokeContext *> (data);
  NPFridaPromise * promise = reinterpret_cast<NPFridaPromise *> (ctx->promise);
  NPFridaObject * self = static_cast<NPFridaNPObject *> (promise->user_data)->g_object;

  if (ctx->error == NULL)
  {
    if (ctx->retval == NULL)
    {
      npfrida_promise_resolve (promise, NULL, 0);
    }
    else
    {
      NPVariant val;
      npfrida_object_return_value_to_npvariant (self, ctx->retval, &val);
      npfrida_promise_resolve (promise, &val, 1);
      npfrida_nsfuncs->releasevariantvalue (&val);
    }
  }
  else
  {
    NPVariant message;

    STRINGZ_TO_NPVARIANT (ctx->error->message, message);
    npfrida_promise_reject (promise, &message, 1);
  }

  g_free (ctx->function_name);
  if (ctx->arguments != NULL)
    g_variant_unref (ctx->arguments);
  npfrida_nsfuncs->releaseobject (ctx->promise);
  if (ctx->retval != NULL)
    g_variant_unref (ctx->retval);
  g_clear_error (&ctx->error);
  g_slice_free (NPFridaInvokeContext, ctx);
}

static bool
npfrida_object_invoke_default (NPObject * npobj, const NPVariant * args, uint32_t arg_count, NPVariant * result)
{
  (void) npobj;
  (void) args;
  (void) arg_count;
  (void) result;

  npfrida_nsfuncs->setexception (npobj, "invoke_default() is not yet implemented");
  return false;
}

static bool
npfrida_object_has_property (NPObject * npobj, NPIdentifier name)
{
  NPFridaNPObjectClass * np_class = reinterpret_cast<NPFridaNPObjectClass *> (npobj->_class);
  NPString * name_str = static_cast<NPString *> (name);

  return g_object_class_find_property (G_OBJECT_CLASS (np_class->g_class), name_str->UTF8Characters) != NULL;
}

static bool
npfrida_object_get_property (NPObject * npobj, NPIdentifier name, NPVariant * result)
{
  NPFridaNPObject * np_object = reinterpret_cast<NPFridaNPObject *> (npobj);
  NPFridaNPObjectClass * np_class = reinterpret_cast<NPFridaNPObjectClass *> (npobj->_class);
  NPFridaGetPropertyContext ctx = { 0, };
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
  g_source_set_callback (source, npfrida_object_do_get_property, &ctx, NULL);
  g_source_attach (source, npfrida_main_context);
  g_source_unref (source);

  G_LOCK (npfrida_object);
  while (!ctx.completed)
    g_cond_wait (&ctx.self->priv->cond, &G_LOCK_NAME (npfrida_object));
  G_UNLOCK (npfrida_object);

  if (!npfrida_object_gvalue_to_npvariant (ctx.self, &ctx.value, result))
    goto cannot_marshal;
  g_value_unset (&ctx.value);

  return true;

  /* ERRORS */
no_such_property:
  {
    npfrida_nsfuncs->setexception (npobj, "no such property");
    return false;
  }
cannot_marshal:
  {
    g_value_unset (&ctx.value);
    npfrida_nsfuncs->setexception (npobj, "type cannot be marshalled");
    return false;
  }
}

static gboolean
npfrida_object_do_get_property (gpointer data)
{
  NPFridaGetPropertyContext * ctx = static_cast<NPFridaGetPropertyContext *> (data);
  NPFridaObjectPrivate * priv = ctx->self->priv;

  g_object_get_property (G_OBJECT (ctx->self), ctx->property_name, &ctx->value);

  G_LOCK (npfrida_object);
  ctx->completed = TRUE;
  g_cond_signal (&priv->cond);
  G_UNLOCK (npfrida_object);

  return FALSE;
}

static bool
npfrida_object_add_event_listener (NPObject * npobj, const NPVariant * args, uint32_t arg_count, NPVariant * result)
{
  const NPVariant * signal_name, * signal_handler;
  gchar * signal_name_str;
  guint signal_id;

  if (arg_count != 2)
  {
    npfrida_nsfuncs->setexception (npobj, "addEventListener requires two arguments");
    return true;
  }

  signal_name = &args[0];
  if (signal_name->type != NPVariantType_String)
  {
    npfrida_nsfuncs->setexception (npobj, "event name must be a string");
    return true;
  }

  signal_handler = &args[1];
  if (signal_handler->type != NPVariantType_Object)
  {
    npfrida_nsfuncs->setexception (npobj, "event handler must be a function");
    return true;
  }

  signal_name_str = (gchar *) g_malloc (signal_name->value.stringValue.UTF8Length + 1);
  memcpy (signal_name_str, signal_name->value.stringValue.UTF8Characters, signal_name->value.stringValue.UTF8Length);
  signal_name_str[signal_name->value.stringValue.UTF8Length] = '\0';
  signal_id = g_signal_lookup (signal_name_str, G_OBJECT_TYPE (reinterpret_cast<NPFridaNPObject *> (npobj)->g_object));
  g_free (signal_name_str), signal_name_str = NULL;

  if (signal_id == 0)
  {
    npfrida_nsfuncs->setexception (npobj, "invalid event name");
    return true;
  }

  g_signal_connect_closure_by_id (reinterpret_cast<NPFridaNPObject *> (npobj)->g_object, signal_id, 0,
      npfrida_closure_new (reinterpret_cast<NPFridaNPObject *> (npobj), signal_handler->value.objectValue), TRUE);

  VOID_TO_NPVARIANT (*result);
  return true;
}

static GVariant *
npfrida_object_argument_list_to_gvariant (NPFridaObject * self, const NPVariant * args, guint arg_count, GError ** err)
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

        str = npfrida_npstring_to_cstring (&var->value.stringValue);
        g_variant_builder_add_value (&builder, g_variant_new_string (str));
        g_free (str);

        break;
      }
      case NPVariantType_Object:
      {
        NPVariant result;

        VOID_TO_NPVARIANT (result);
        if (npfrida_nsfuncs->invoke (self->priv->npp, self->priv->json, npfrida_nsfuncs->getstringidentifier ("stringify"), var, 1, &result))
        {
          gchar * str;

          str = npfrida_npstring_to_cstring (&result.value.stringValue);
          g_variant_builder_add_value (&builder, g_variant_new_string (str));
          g_free (str);

          npfrida_nsfuncs->releasevariantvalue (&result);

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
npfrida_object_return_value_to_npvariant (NPFridaObject * self, GVariant * retval, NPVariant * result)
{
  if (retval == NULL)
  {
    VOID_TO_NPVARIANT (*result);
    return;
  }

  npfrida_object_gvariant_to_npvariant (self, retval, result);
}

static void
npfrida_object_gvariant_to_npvariant (NPFridaObject * self, GVariant * retval, NPVariant * result)
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
    npfrida_nsfuncs->invoke (self->priv->npp, self->priv->json, npfrida_nsfuncs->getstringidentifier ("parse"), &variant, 1, result);
  }
  else
  {
    g_assert_not_reached ();
  }
}

static gboolean
npfrida_object_gvalue_to_npvariant (NPFridaObject * self, const GValue * gvalue, NPVariant * result)
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
      npfrida_nsfuncs->invoke (self->priv->npp, self->priv->json, npfrida_nsfuncs->getstringidentifier ("parse"), &variant, 1, result);
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
        OBJECT_TO_NPVARIANT (npfrida_byte_array_new (self->priv->npp,
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

static NPClass npfrida_object_template_np_class =
{
  NP_CLASS_STRUCT_VERSION,
  npfrida_object_allocate,
  npfrida_object_deallocate,
  npfrida_object_invalidate,
  npfrida_object_has_method,
  npfrida_object_invoke,
  npfrida_object_invoke_default,
  npfrida_object_has_property,
  npfrida_object_get_property,
  NULL,
  NULL,
  NULL,
  NULL
};

G_LOCK_DEFINE_STATIC (np_class_by_gobject_class);
static GHashTable * np_class_by_gobject_class = NULL;

void
npfrida_object_type_init (void)
{
  np_class_by_gobject_class = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_type_class_unref, g_free);
}

void
npfrida_object_type_deinit (void)
{
  if (np_class_by_gobject_class != NULL)
  {
    g_hash_table_unref (np_class_by_gobject_class);
    np_class_by_gobject_class = NULL;
  }
}

gpointer
npfrida_object_type_get_np_class (GType gtype)
{
  NPFridaObjectClass * gobject_class;
  NPFridaNPObjectClass * np_class;

  g_assert (g_type_is_a (gtype, NPFRIDA_TYPE_OBJECT));

  gobject_class = NPFRIDA_OBJECT_CLASS (g_type_class_ref (gtype));

  G_LOCK (np_class_by_gobject_class);

  np_class = static_cast<NPFridaNPObjectClass *> (g_hash_table_lookup (np_class_by_gobject_class, gobject_class));
  if (np_class == NULL)
  {
    np_class = g_new (NPFridaNPObjectClass, 1);

    memcpy (np_class, &npfrida_object_template_np_class, sizeof (npfrida_object_template_np_class));

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
npfrida_closure_new (NPFridaNPObject * object, NPObject * callback)
{
  GClosure * closure;
  NPFridaClosure * self;

  closure = g_closure_new_simple (sizeof (NPFridaClosure), NULL);
  g_closure_add_finalize_notifier (closure, NULL, npfrida_closure_finalize);
  self = reinterpret_cast<NPFridaClosure *> (closure);
  self->object = object;
  self->callback = npfrida_nsfuncs->retainobject (callback);

  g_closure_set_marshal (closure, npfrida_closure_marshal);

  return closure;
}

static void
npfrida_closure_finalize (gpointer data, GClosure * closure)
{
  NPFridaClosure * self = reinterpret_cast<NPFridaClosure *> (closure);

  (void) data;

  npfrida_nsfuncs->releaseobject (self->callback);
}

static void
npfrida_closure_marshal (GClosure * closure, GValue * return_gvalue, guint n_param_values, const GValue * param_values,
    gpointer invocation_hint, gpointer marshal_data)
{
  NPFridaClosure * self = reinterpret_cast<NPFridaClosure *> (closure);
  NPFridaClosureInvocation * invocation;
  guint i;

  (void) return_gvalue;
  (void) invocation_hint;
  (void) marshal_data;

  invocation = g_slice_new (NPFridaClosureInvocation);
  invocation->closure = self;
  invocation->args = g_array_sized_new (FALSE, FALSE, sizeof (GValue), n_param_values);
  for (i = 0; i != n_param_values; i++)
  {
    GValue val = { 0, };
    g_value_copy (&param_values[i], &val);
    g_array_append_val (invocation->args, val);
  }

  npfrida_nsfuncs->pluginthreadasynccall (self->object->g_object->priv->npp, npfrida_closure_invoke, invocation);
}

static void
npfrida_closure_invoke (void * data)
{
  NPFridaClosureInvocation * invocation = static_cast<NPFridaClosureInvocation *> (data);
  NPFridaClosure * self = invocation->closure;
  NPVariant * args;
  guint arg_count = invocation->args->len - 1;
  gboolean success = TRUE;
  guint i;

  args = static_cast<NPVariant *> (g_alloca (arg_count * sizeof (NPVariant)));
  for (i = 1; i != invocation->args->len; i++)
  {
    if (!npfrida_object_gvalue_to_npvariant (self->object->g_object, &g_array_index (invocation->args, GValue, i), &args[i - 1]))
    {
      success = FALSE;
      g_debug ("failed to convert argument %u to a variant", i - 1);
    }
  }

  if (success)
  {
    NPVariant result;

    VOID_TO_NPVARIANT (result);
    npfrida_nsfuncs->invokeDefault (self->object->g_object->priv->npp, self->callback, args, arg_count, &result);
    npfrida_nsfuncs->releasevariantvalue (&result);
  }

  for (i = 0; i != arg_count; i++)
    npfrida_nsfuncs->releasevariantvalue (&args[i]);

  for (i = 0; i != invocation->args->len; i++)
    g_value_reset (&g_array_index (invocation->args, GValue, i));
  g_array_free (invocation->args, TRUE);
  g_slice_free (NPFridaClosureInvocation, invocation);
}
