#include <glib.h>
#include <glib-object.h>
#include <cloud-spy-object.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>

static const gint cloud_spy_dispatcher_magic = 1337;

typedef struct _CloudSpyDispatcherInvocation CloudSpyDispatcherInvocation;

struct _CloudSpyDispatcherInvocation
{
  const gint * magic;

  volatile gint ref_count;

  GDBusMethodInfo * method;
  GVariant * parameters;
  GSimpleAsyncResult * res;

  GDBusMessage * call_message;
};

static void cloud_spy_dispatcher_unref (gpointer object);

static CloudSpyDispatcherInvocation * cloud_spy_dispatcher_invocation_new (GDBusMethodInfo * method, GVariant * parameters, GSimpleAsyncResult * res);
static void cloud_spy_dispatcher_invocation_ref (CloudSpyDispatcherInvocation * invocation);
static void cloud_spy_dispatcher_invocation_unref (CloudSpyDispatcherInvocation * invocation);
static void cloud_spy_dispatcher_invocation_perform (CloudSpyDispatcherInvocation * self);

static GDBusMessage * cloud_spy_dispatcher_invocation_get_message (GDBusMethodInvocation * invocation);
static GDBusConnection * cloud_spy_dispatcher_invocation_get_connection (GDBusMethodInvocation * invocation);
static gboolean cloud_spy_dispatcher_connection_send_message (GDBusConnection * connection, GDBusMessage * message, GDBusSendMessageFlags flags, volatile guint32 * out_serial, GError ** error);
static void cloud_spy_dispatcher_invocation_return_gerror (GDBusMethodInvocation * invocation, const GError * error);

#define g_object_unref(obj) \
    cloud_spy_dispatcher_unref (obj)
#define g_dbus_method_invocation_get_message(invocation) \
    cloud_spy_dispatcher_invocation_get_message (invocation)
#define g_dbus_method_invocation_get_connection(invocation) \
    cloud_spy_dispatcher_invocation_get_connection (invocation)
#define g_dbus_connection_send_message(connection, message, flags, out_serial, error) \
    cloud_spy_dispatcher_connection_send_message (connection, message, flags, out_serial, error)
#define g_dbus_method_invocation_return_gerror(invocation, error) \
    cloud_spy_dispatcher_invocation_return_gerror (invocation, error)

#ifdef _MSC_VER
# pragma warning (push)
# pragma warning (disable: 4054 4055 4090 4100 4152 4189 4267 4702)
#endif
#include "cloud-spy-api.c"
#ifdef _MSC_VER
# pragma warning (pop)
#endif

#undef g_object_unref
#undef g_dbus_method_invocation_get_message
#undef g_dbus_method_invocation_get_connection
#undef g_dbus_connection_send_message
#undef g_dbus_method_invocation_return_gerror

void
cloud_spy_dispatcher_init_with_object (CloudSpyDispatcher * self, CloudSpyObject * obj)
{
  GType type;

  self->target_object = obj;

  type = G_TYPE_FROM_INSTANCE (obj);

  if (g_type_is_a (type, CLOUD_SPY_TYPE_ROOT_API))
  {
    self->methods = (GDBusMethodInfo **) _cloud_spy_root_api_dbus_method_info;
    self->dispatch_func = cloud_spy_root_api_dbus_interface_method_call;
  }
  else
    g_assert_not_reached ();
}

static void
cloud_spy_dispatcher_do_invoke (CloudSpyDispatcher * self, GDBusMethodInfo * method, GVariant * parameters,
    GAsyncReadyCallback callback, gpointer user_data)
{
  CloudSpyDispatcherInvocation * invocation;

  invocation = cloud_spy_dispatcher_invocation_new (method, parameters,
      g_simple_async_result_new (G_OBJECT (self), callback, user_data, GSIZE_TO_POINTER (cloud_spy_dispatcher_do_invoke)));
  cloud_spy_dispatcher_invocation_perform (invocation);
  cloud_spy_dispatcher_invocation_unref (invocation);
}

static GVariant *
cloud_spy_dispatcher_do_invoke_finish (CloudSpyDispatcher * self, GAsyncResult * res, GError ** error)
{
  GSimpleAsyncResult * ar = G_SIMPLE_ASYNC_RESULT (res);

  (void) self;

  if (g_simple_async_result_propagate_error (ar, error))
    return NULL;

  return g_variant_ref (g_simple_async_result_get_op_res_gpointer (ar));
}

static void
cloud_spy_dispatcher_unref (gpointer object)
{
  CloudSpyDispatcherInvocation * invocation = object;
  if (invocation->magic == &cloud_spy_dispatcher_magic)
    cloud_spy_dispatcher_invocation_unref (invocation);
  else
    g_object_unref (object);
}

static CloudSpyDispatcherInvocation *
cloud_spy_dispatcher_invocation_new (GDBusMethodInfo * method, GVariant * parameters, GSimpleAsyncResult * res)
{
  CloudSpyDispatcherInvocation * invocation;

  invocation = g_slice_new (CloudSpyDispatcherInvocation);
  invocation->magic = &cloud_spy_dispatcher_magic;

  invocation->ref_count = 1;

  invocation->method = g_dbus_method_info_ref (method);
  invocation->parameters = g_variant_ref (parameters);
  invocation->res = res;

  invocation->call_message = g_dbus_message_new_method_call (NULL, "/org/boblycat/frida/Foo", "org.boblycat.Frida.Foo", method->name);
  g_dbus_message_set_serial (invocation->call_message, 1);

  return invocation;
}

static void
cloud_spy_dispatcher_invocation_ref (CloudSpyDispatcherInvocation * invocation)
{
  g_atomic_int_inc (&invocation->ref_count);
}

static void
cloud_spy_dispatcher_invocation_unref (CloudSpyDispatcherInvocation * invocation)
{
  if (g_atomic_int_dec_and_test (&invocation->ref_count))
  {
    g_dbus_method_info_unref (invocation->method);
    g_variant_unref (invocation->parameters);
    g_object_unref (invocation->res);

    g_object_unref (invocation->call_message);

    g_slice_free (CloudSpyDispatcherInvocation, invocation);
  }
}

static void
cloud_spy_dispatcher_invocation_perform (CloudSpyDispatcherInvocation * self)
{
  CloudSpyDispatcher * dispatcher;

  dispatcher = CLOUD_SPY_DISPATCHER (g_async_result_get_source_object (G_ASYNC_RESULT (self->res)));

  cloud_spy_dispatcher_invocation_ref (self);
  dispatcher->dispatch_func (NULL, NULL, NULL, NULL, self->method->name, self->parameters,
      (GDBusMethodInvocation *) self, &dispatcher->target_object);

  g_object_unref (dispatcher);
}

static GDBusMessage *
cloud_spy_dispatcher_invocation_get_message (GDBusMethodInvocation * invocation)
{
  return ((CloudSpyDispatcherInvocation *) invocation)->call_message;
}

static GDBusConnection *
cloud_spy_dispatcher_invocation_get_connection (GDBusMethodInvocation * invocation)
{
  return (GDBusConnection *) invocation;
}

static gboolean
cloud_spy_dispatcher_connection_send_message (GDBusConnection * connection, GDBusMessage * message, GDBusSendMessageFlags flags, volatile guint32 * out_serial, GError ** error)
{
  CloudSpyDispatcherInvocation * self = (CloudSpyDispatcherInvocation *) connection;
  GVariant * result;

  (void) flags;
  (void) out_serial;
  (void) error;

  result = g_variant_ref (g_dbus_message_get_body (message));
  g_simple_async_result_set_op_res_gpointer (self->res, result, g_variant_unref);
  g_simple_async_result_complete (self->res);

  return TRUE;
}

static void
cloud_spy_dispatcher_invocation_return_gerror (GDBusMethodInvocation * invocation, const GError * error)
{
  CloudSpyDispatcherInvocation * self = (CloudSpyDispatcherInvocation *) invocation;

  g_simple_async_result_take_error (self->res, (GError *) error);
  g_simple_async_result_complete (self->res);

  cloud_spy_dispatcher_invocation_unref (self);
}
