#include "cloud-spy-promise.h"

#include "cloud-spy-plugin.h"

#include <string.h>

static void cloud_spy_promise_deliver (CloudSpyPromise * self, CloudSpyPromiseResult result, const NPVariant * args, guint arg_count);
static void cloud_spy_promise_flush (void * data);
static void cloud_spy_promise_flush_unlocked (CloudSpyPromise * self);
static void cloud_spy_promise_invoke_callback (gpointer data, gpointer user_data);

static NPObject *
cloud_spy_promise_allocate (NPP npp, NPClass * klass)
{
  CloudSpyPromise * promise;

  (void) klass;

  promise = g_slice_new0 (CloudSpyPromise);
  promise->npp = npp;

  promise->mutex = g_mutex_new ();

  promise->result = CLOUD_SPY_PROMISE_PENDING;
  promise->args = g_array_new (FALSE, FALSE, sizeof (NPVariant));

  promise->on_success = g_ptr_array_new_with_free_func (cloud_spy_npobject_release);
  promise->on_failure = g_ptr_array_new_with_free_func (cloud_spy_npobject_release);
  promise->on_complete = g_ptr_array_new_with_free_func (cloud_spy_npobject_release);

  return &promise->np_object;
}

static void
cloud_spy_promise_deallocate (NPObject * npobj)
{
  CloudSpyPromise * promise = reinterpret_cast<CloudSpyPromise *> (npobj);
  guint i;

  if (promise->destroy_user_data != NULL)
    promise->destroy_user_data (promise->user_data);

  g_mutex_free (promise->mutex);

  for (i = 0; i != promise->args->len; i++)
    cloud_spy_nsfuncs->releasevariantvalue (&g_array_index (promise->args, NPVariant, i));
  g_array_unref (promise->args);

  g_ptr_array_unref (promise->on_success);
  g_ptr_array_unref (promise->on_failure);
  g_ptr_array_unref (promise->on_complete);

  g_slice_free (CloudSpyPromise, promise);
}

static void
cloud_spy_promise_invalidate (NPObject * npobj)
{
  (void) npobj;
}

void
cloud_spy_promise_resolve (CloudSpyPromise * self, const NPVariant * args, guint arg_count)
{
  cloud_spy_promise_deliver (self, CLOUD_SPY_PROMISE_SUCCESS, args, arg_count);
}

void
cloud_spy_promise_reject (CloudSpyPromise * self, const NPVariant * args, guint arg_count)
{
  cloud_spy_promise_deliver (self, CLOUD_SPY_PROMISE_FAILURE, args, arg_count);
}

static void
cloud_spy_promise_deliver (CloudSpyPromise * self, CloudSpyPromiseResult result, const NPVariant * args, guint arg_count)
{
  guint i;

  g_mutex_lock (self->mutex);

  g_assert (self->result == CLOUD_SPY_PROMISE_PENDING);
  self->result = result;

  g_array_set_size (self->args, arg_count);
  for (i = 0; i != arg_count; i++)
    cloud_spy_init_npvariant_with_other (&g_array_index (self->args, NPVariant, i), &args[i]);

  g_mutex_unlock (self->mutex);

  cloud_spy_nsfuncs->retainobject (&self->np_object);
  cloud_spy_nsfuncs->pluginthreadasynccall (self->npp, cloud_spy_promise_flush, self);
}

static void
cloud_spy_promise_flush (void * data)
{
  CloudSpyPromise * self = static_cast<CloudSpyPromise *> (data);

  g_mutex_lock (self->mutex);
  cloud_spy_promise_flush_unlocked (self);
  g_mutex_unlock (self->mutex);

  cloud_spy_nsfuncs->releaseobject (&self->np_object);
}

static void
cloud_spy_promise_flush_unlocked (CloudSpyPromise * self)
{
  GPtrArray * on_success, * on_failure, * on_complete;

  if (self->result == CLOUD_SPY_PROMISE_PENDING)
    return;

  on_success = self->on_success; self->on_success = g_ptr_array_new_with_free_func (cloud_spy_npobject_release);
  on_failure = self->on_failure; self->on_failure = g_ptr_array_new_with_free_func (cloud_spy_npobject_release);
  on_complete = self->on_complete; self->on_complete = g_ptr_array_new_with_free_func (cloud_spy_npobject_release);

  g_mutex_unlock (self->mutex);

  if (self->result == CLOUD_SPY_PROMISE_SUCCESS)
    g_ptr_array_foreach (on_success, cloud_spy_promise_invoke_callback, self);
  else
    g_ptr_array_foreach (on_failure, cloud_spy_promise_invoke_callback, self);
  g_ptr_array_foreach (on_complete, cloud_spy_promise_invoke_callback, self);

  g_ptr_array_unref (on_success);
  g_ptr_array_unref (on_failure);
  g_ptr_array_unref (on_complete);

  g_mutex_lock (self->mutex);
}

static void
cloud_spy_promise_invoke_callback (gpointer data, gpointer user_data)
{
  CloudSpyPromise * self = static_cast<CloudSpyPromise *> (user_data);
  NPObject * callback = static_cast<NPObject *> (data);
  NPVariant result;

  VOID_TO_NPVARIANT (result);
  cloud_spy_nsfuncs->invokeDefault (self->npp, callback, &g_array_index (self->args, NPVariant, 0), self->args->len, &result);
  cloud_spy_nsfuncs->releasevariantvalue (&result);
}

static bool
cloud_spy_promise_has_method (NPObject * npobj, NPIdentifier name)
{
  const gchar * function_name = static_cast<NPString *> (name)->UTF8Characters;

  (void) npobj;

  return (strcmp (function_name, "always") == 0 ||
      strcmp (function_name, "done") == 0 ||
      strcmp (function_name, "fail") == 0 ||
      strcmp (function_name, "state") == 0);
}

static bool
cloud_spy_promise_invoke (NPObject * npobj, NPIdentifier name, const NPVariant * args, uint32_t arg_count, NPVariant * result)
{
  CloudSpyPromise * self = reinterpret_cast<CloudSpyPromise *> (npobj);
  const gchar * function_name = static_cast<NPString *> (name)->UTF8Characters;
  NPObject * callback;
  GPtrArray * callbacks;

  if (strcmp (function_name, "state") == 0)
  {
    const gchar * state = NULL;

    if (arg_count != 0)
    {
      cloud_spy_nsfuncs->setexception (npobj, "invalid argument");
      return true;
    }

    switch (self->result)
    {
      case CLOUD_SPY_PROMISE_PENDING:
        state = "pending";
        break;
      case CLOUD_SPY_PROMISE_SUCCESS:
        state = "resolved";
        break;
      case CLOUD_SPY_PROMISE_FAILURE:
        state = "rejected";
        break;
    }

    cloud_spy_init_npvariant_with_string (result, state);
  }
  else
  {
    if (arg_count != 1 || args[0].type != NPVariantType_Object)
    {
      cloud_spy_nsfuncs->setexception (npobj, "invalid argument");
      return true;
    }

    callback = cloud_spy_nsfuncs->retainobject (NPVARIANT_TO_OBJECT (args[0]));

    g_mutex_lock (self->mutex);
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
    cloud_spy_promise_flush_unlocked (self);
    g_mutex_unlock (self->mutex);

    OBJECT_TO_NPVARIANT (cloud_spy_nsfuncs->retainobject (npobj), *result);
  }

  return true;
}

static NPClass cloud_spy_promise_np_class =
{
  NP_CLASS_STRUCT_VERSION,
  cloud_spy_promise_allocate,
  cloud_spy_promise_deallocate,
  cloud_spy_promise_invalidate,
  cloud_spy_promise_has_method,
  cloud_spy_promise_invoke,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

NPObject *
cloud_spy_promise_new (NPP npp, gpointer user_data, GDestroyNotify destroy_user_data)
{
  CloudSpyPromise * promise;

  promise = reinterpret_cast<CloudSpyPromise *> (cloud_spy_nsfuncs->createobject (npp, &cloud_spy_promise_np_class));
  promise->user_data = user_data;
  promise->destroy_user_data = destroy_user_data;

  return &promise->np_object;
}
