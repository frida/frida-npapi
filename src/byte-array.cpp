#include "cloud-spy-byte-array.h"

#include <string.h>

typedef struct _CloudSpyByteArray CloudSpyByteArray;

struct _CloudSpyByteArray
{
  NPObject np_object;
  guint8 * data;
  gint data_length;
};

static NPObject *
cloud_spy_variant_allocate (NPP npp, NPClass * klass)
{
  CloudSpyByteArray * obj;

  (void) npp;
  (void) klass;

  obj = g_slice_new (CloudSpyByteArray);
  obj->data = NULL;
  obj->data_length = 0;

  return &obj->np_object;
}

static void
cloud_spy_variant_deallocate (NPObject * npobj)
{
  CloudSpyByteArray * self = reinterpret_cast<CloudSpyByteArray *> (npobj);

  g_free (self->data);

  g_slice_free (CloudSpyByteArray, self);
}

static void
cloud_spy_variant_invalidate (NPObject * npobj)
{
  (void) npobj;
}

static bool
cloud_spy_variant_has_method (NPObject * npobj, NPIdentifier name)
{
  const gchar * method_name;

  (void) npobj;

  method_name = static_cast<NPString *> (name)->UTF8Characters;
  if (strcmp (method_name, "toString") == 0)
    return true;
  else if (strcmp (method_name, "splice") == 0)
    return true;

  return false;
}

static bool
cloud_spy_variant_invoke (NPObject * npobj, NPIdentifier name, const NPVariant * args, uint32_t arg_count, NPVariant * result)
{
  CloudSpyByteArray * self = reinterpret_cast<CloudSpyByteArray *> (npobj);

  (void) args;

  if (strcmp (static_cast<NPString *> (name)->UTF8Characters, "toString") == 0 && arg_count <= 1)
  {
    gboolean base64 = FALSE;

    if (arg_count == 1)
    {
      if (args[0].type == NPVariantType_String)
      {
        gchar * format = cloud_spy_npstring_to_cstring (&args[0].value.stringValue);

        if (strcmp (format, "base64") == 0)
        {
          base64 = TRUE;
        }
        else if (strcmp (format, "hex") == 0)
        {
          base64 = FALSE;
        }
        else
        {
          g_free (format);
          cloud_spy_nsfuncs->setexception (npobj, "invalid format specified");
          return true;
        }

        g_free (format);
      }
      else
      {
        cloud_spy_nsfuncs->setexception (npobj, "invalid argument");
        return true;
      }
    }

    if (base64)
    {
      gchar * str = g_base64_encode (self->data, self->data_length);
      cloud_spy_init_npvariant_with_string (result, str);
      g_free (str);
    }
    else
    {
      GString * s;
      gint i;

      s = g_string_sized_new (3 * self->data_length);
      for (i = 0; i != self->data_length; i++)
      {
        if (i != 0)
          g_string_append_c (s, ' ');
        g_string_append_printf (s, "%02x", (gint) self->data[i]);
      }

      cloud_spy_init_npvariant_with_string (result, s->str);

      g_string_free (s, TRUE);
    }

    return true;
  }
  else
  {
    cloud_spy_nsfuncs->setexception (npobj, "no such method");
    return true;
  }
}

static bool
cloud_spy_variant_invoke_default (NPObject * npobj, const NPVariant * args, uint32_t arg_count, NPVariant * result)
{
  (void) args;
  (void) arg_count;
  (void) result;

  cloud_spy_nsfuncs->setexception (npobj, "invalid operation");
  return true;
}

static bool
cloud_spy_variant_has_property (NPObject * npobj, NPIdentifier name)
{
  CloudSpyByteArray * self = reinterpret_cast<CloudSpyByteArray *> (npobj);

  if (cloud_spy_nsfuncs->identifierisstring (name))
  {
    const gchar * property_name;

    property_name = static_cast<NPString *> (name)->UTF8Characters;
    if (strcmp (property_name, "length") == 0)
      return true;
  }
  else
  {
    int32_t index;

    index = cloud_spy_nsfuncs->intfromidentifier (name);
    if (index >= 0 && index < self->data_length)
      return true;
  }

  return false;
}

static bool
cloud_spy_variant_get_property (NPObject * npobj, NPIdentifier name, NPVariant * result)
{
  CloudSpyByteArray * self = reinterpret_cast<CloudSpyByteArray *> (npobj);

  if (cloud_spy_nsfuncs->identifierisstring (name))
  {
    const gchar * property_name;

    property_name = static_cast<NPString *> (name)->UTF8Characters;
    if (strcmp (property_name, "length") == 0)
    {
      INT32_TO_NPVARIANT (self->data_length, *result);
      return true;
    }
  }
  else
  {
    int32_t index;

    index = cloud_spy_nsfuncs->intfromidentifier (name);
    if (index >= 0 && index < self->data_length)
    {
      INT32_TO_NPVARIANT (self->data[index], *result);
      return true;
    }
  }

  cloud_spy_nsfuncs->setexception (npobj, "invalid property");
  return true;
}

static NPClass cloud_spy_variant_class =
{
  NP_CLASS_STRUCT_VERSION,
  cloud_spy_variant_allocate,
  cloud_spy_variant_deallocate,
  cloud_spy_variant_invalidate,
  cloud_spy_variant_has_method,
  cloud_spy_variant_invoke,
  cloud_spy_variant_invoke_default,
  cloud_spy_variant_has_property,
  cloud_spy_variant_get_property,
  NULL,
  NULL,
  NULL,
  NULL
};

NPObject *
cloud_spy_byte_array_new (NPP npp, const guint8 * data, gint data_length)
{
  CloudSpyByteArray * obj;

  obj = reinterpret_cast<CloudSpyByteArray *> (cloud_spy_nsfuncs->createobject (npp, &cloud_spy_variant_class));
  obj->data = (data_length != 0) ? static_cast<guint8 *> (g_memdup (data, data_length)) : NULL;
  obj->data_length = data_length;

  return &obj->np_object;
}

NPClass *
cloud_spy_variant_get_class (void)
{
  return &cloud_spy_variant_class;
}
