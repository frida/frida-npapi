#include "npfrida-promise.h"

#include "npfrida-plugin.h"

#include <string.h>

#define NPFRIDA_PROMISE_LOCK() \
    g_mutex_lock (&self->mutex)
#define NPFRIDA_PROMISE_UNLOCK() \
    g_mutex_unlock (&self->mutex)

static void npfrida_promise_deliver (NPFridaPromise * self, NPFridaPromiseResult result, const NPVariant * args, guint arg_count);
static void npfrida_promise_flush (void * data);
static void npfrida_promise_flush_unlocked (NPFridaPromise * self);
static void npfrida_promise_invoke_callback (gpointer data, gpointer user_data);

static NPObject *
npfrida_promise_allocate (NPP npp, NPClass * klass)
{
  NPFridaPromise * promise;

  (void) klass;

  promise = g_slice_new0 (NPFridaPromise);
  promise->npp = npp;

  g_mutex_init (&promise->mutex);

  promise->result = NPFRIDA_PROMISE_PENDING;
  promise->args = g_array_new (FALSE, FALSE, sizeof (NPVariant));

  promise->on_success = g_ptr_array_new_with_free_func (npfrida_npobject_release);
  promise->on_failure = g_ptr_array_new_with_free_func (npfrida_npobject_release);
  promise->on_complete = g_ptr_array_new_with_free_func (npfrida_npobject_release);

  return &promise->np_object;
}

static void
npfrida_promise_deallocate (NPObject * npobj)
{
  NPFridaPromise * promise = reinterpret_cast<NPFridaPromise *> (npobj);
  guint i;

  if (promise->destroy_user_data != NULL)
    promise->destroy_user_data (promise->user_data);

  g_mutex_clear (&promise->mutex);

  for (i = 0; i != promise->args->len; i++)
    npfrida_nsfuncs->releasevariantvalue (&g_array_index (promise->args, NPVariant, i));
  g_array_unref (promise->args);

  g_ptr_array_unref (promise->on_success);
  g_ptr_array_unref (promise->on_failure);
  g_ptr_array_unref (promise->on_complete);

  g_slice_free (NPFridaPromise, promise);
}

static void
npfrida_promise_invalidate (NPObject * npobj)
{
  (void) npobj;
}

void
npfrida_promise_resolve (NPFridaPromise * self, const NPVariant * args, guint arg_count)
{
  npfrida_promise_deliver (self, NPFRIDA_PROMISE_SUCCESS, args, arg_count);
}

void
npfrida_promise_reject (NPFridaPromise * self, const NPVariant * args, guint arg_count)
{
  npfrida_promise_deliver (self, NPFRIDA_PROMISE_FAILURE, args, arg_count);
}

static void
npfrida_promise_deliver (NPFridaPromise * self, NPFridaPromiseResult result, const NPVariant * args, guint arg_count)
{
  guint i;

  NPFRIDA_PROMISE_LOCK ();

  g_assert (self->result == NPFRIDA_PROMISE_PENDING);
  self->result = result;

  g_array_set_size (self->args, arg_count);
  for (i = 0; i != arg_count; i++)
    npfrida_init_npvariant_with_other (&g_array_index (self->args, NPVariant, i), &args[i]);

  NPFRIDA_PROMISE_UNLOCK ();

  npfrida_nsfuncs->retainobject (&self->np_object);
  npfrida_nsfuncs->pluginthreadasynccall (self->npp, npfrida_promise_flush, self);
}

static void
npfrida_promise_flush (void * data)
{
  NPFridaPromise * self = static_cast<NPFridaPromise *> (data);

  NPFRIDA_PROMISE_LOCK ();
  npfrida_promise_flush_unlocked (self);
  NPFRIDA_PROMISE_UNLOCK ();

  npfrida_nsfuncs->releaseobject (&self->np_object);
}

static void
npfrida_promise_flush_unlocked (NPFridaPromise * self)
{
  GPtrArray * on_success, * on_failure, * on_complete;

  if (self->result == NPFRIDA_PROMISE_PENDING)
    return;

  on_success = self->on_success; self->on_success = g_ptr_array_new_with_free_func (npfrida_npobject_release);
  on_failure = self->on_failure; self->on_failure = g_ptr_array_new_with_free_func (npfrida_npobject_release);
  on_complete = self->on_complete; self->on_complete = g_ptr_array_new_with_free_func (npfrida_npobject_release);

  NPFRIDA_PROMISE_UNLOCK ();

  if (self->result == NPFRIDA_PROMISE_SUCCESS)
    g_ptr_array_foreach (on_success, npfrida_promise_invoke_callback, self);
  else
    g_ptr_array_foreach (on_failure, npfrida_promise_invoke_callback, self);
  g_ptr_array_foreach (on_complete, npfrida_promise_invoke_callback, self);

  g_ptr_array_unref (on_success);
  g_ptr_array_unref (on_failure);
  g_ptr_array_unref (on_complete);

  NPFRIDA_PROMISE_LOCK ();
}

static void
npfrida_promise_invoke_callback (gpointer data, gpointer user_data)
{
  NPFridaPromise * self = static_cast<NPFridaPromise *> (user_data);
  NPObject * callback = static_cast<NPObject *> (data);
  NPVariant result;

  VOID_TO_NPVARIANT (result);
  npfrida_nsfuncs->invokeDefault (self->npp, callback, &g_array_index (self->args, NPVariant, 0), self->args->len, &result);
  npfrida_nsfuncs->releasevariantvalue (&result);
}

static bool
npfrida_promise_has_method (NPObject * npobj, NPIdentifier name)
{
  const gchar * function_name = static_cast<NPString *> (name)->UTF8Characters;

  (void) npobj;

  return (strcmp (function_name, "always") == 0 ||
      strcmp (function_name, "done") == 0 ||
      strcmp (function_name, "fail") == 0 ||
      strcmp (function_name, "state") == 0);
}

static bool
npfrida_promise_invoke (NPObject * npobj, NPIdentifier name, const NPVariant * args, uint32_t arg_count, NPVariant * result)
{
  NPFridaPromise * self = reinterpret_cast<NPFridaPromise *> (npobj);
  const gchar * function_name = static_cast<NPString *> (name)->UTF8Characters;
  NPObject * callback;
  GPtrArray * callbacks;

  if (strcmp (function_name, "state") == 0)
  {
    const gchar * state = NULL;

    if (arg_count != 0)
    {
      npfrida_nsfuncs->setexception (npobj, "invalid argument");
      return true;
    }

    switch (self->result)
    {
      case NPFRIDA_PROMISE_PENDING:
        state = "pending";
        break;
      case NPFRIDA_PROMISE_SUCCESS:
        state = "resolved";
        break;
      case NPFRIDA_PROMISE_FAILURE:
        state = "rejected";
        break;
    }

    npfrida_init_npvariant_with_string (result, state);
  }
  else
  {
    if (arg_count != 1 || args[0].type != NPVariantType_Object)
    {
      npfrida_nsfuncs->setexception (npobj, "invalid argument");
      return true;
    }

    callback = npfrida_nsfuncs->retainobject (NPVARIANT_TO_OBJECT (args[0]));

    NPFRIDA_PROMISE_LOCK ();
    if (strcmp (function_name, "done") == 0)
      callbacks = self->on_success;
    else if (strcmp (function_name, "fail") == 0)
      callbacks = self->on_failure;
    else if (strcmp (function_name, "always") == 0)
      callbacks = self->on_complete;
    else
      callbacks = NULL;
    g_assert (callbacks != NULL);

    g_ptr_array_add (callbacks, callback);
    npfrida_promise_flush_unlocked (self);
    NPFRIDA_PROMISE_UNLOCK ();

    OBJECT_TO_NPVARIANT (npfrida_nsfuncs->retainobject (npobj), *result);
  }

  return true;
}

static NPClass npfrida_promise_np_class =
{
  NP_CLASS_STRUCT_VERSION,
  npfrida_promise_allocate,
  npfrida_promise_deallocate,
  npfrida_promise_invalidate,
  npfrida_promise_has_method,
  npfrida_promise_invoke,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

NPObject *
npfrida_promise_new (NPP npp, gpointer user_data, GDestroyNotify destroy_user_data)
{
  NPFridaPromise * promise;

  promise = reinterpret_cast<NPFridaPromise *> (npfrida_nsfuncs->createobject (npp, &npfrida_promise_np_class));
  promise->user_data = user_data;
  promise->destroy_user_data = destroy_user_data;

  return &promise->np_object;
}
