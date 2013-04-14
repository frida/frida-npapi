#include "npfrida-byte-array.h"

#include <string.h>

typedef struct _NPFridaByteArray NPFridaByteArray;

struct _NPFridaByteArray
{
  NPObject np_object;
  guint8 * data;
  gint data_length;
};

static NPObject *
npfrida_variant_allocate (NPP npp, NPClass * klass)
{
  NPFridaByteArray * obj;

  (void) npp;
  (void) klass;

  obj = g_slice_new (NPFridaByteArray);
  obj->data = NULL;
  obj->data_length = 0;

  return &obj->np_object;
}

static void
npfrida_variant_deallocate (NPObject * npobj)
{
  NPFridaByteArray * self = reinterpret_cast<NPFridaByteArray *> (npobj);

  g_free (self->data);

  g_slice_free (NPFridaByteArray, self);
}

static void
npfrida_variant_invalidate (NPObject * npobj)
{
  (void) npobj;
}

static bool
npfrida_variant_has_method (NPObject * npobj, NPIdentifier name)
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
npfrida_variant_invoke (NPObject * npobj, NPIdentifier name, const NPVariant * args, uint32_t arg_count, NPVariant * result)
{
  NPFridaByteArray * self = reinterpret_cast<NPFridaByteArray *> (npobj);

  (void) args;

  if (strcmp (static_cast<NPString *> (name)->UTF8Characters, "toString") == 0 && arg_count <= 1)
  {
    gboolean base64 = FALSE;

    if (arg_count == 1)
    {
      if (args[0].type == NPVariantType_String)
      {
        gchar * format = npfrida_npstring_to_cstring (&args[0].value.stringValue);

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
          npfrida_nsfuncs->setexception (npobj, "invalid format specified");
          return true;
        }

        g_free (format);
      }
      else
      {
        npfrida_nsfuncs->setexception (npobj, "invalid argument");
        return true;
      }
    }

    if (base64)
    {
      gchar * str = g_base64_encode (self->data, self->data_length);
      npfrida_init_npvariant_with_string (result, str);
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

      npfrida_init_npvariant_with_string (result, s->str);

      g_string_free (s, TRUE);
    }

    return true;
  }
  else
  {
    npfrida_nsfuncs->setexception (npobj, "no such method");
    return true;
  }
}

static bool
npfrida_variant_invoke_default (NPObject * npobj, const NPVariant * args, uint32_t arg_count, NPVariant * result)
{
  (void) args;
  (void) arg_count;
  (void) result;

  npfrida_nsfuncs->setexception (npobj, "invalid operation");
  return true;
}

static bool
npfrida_variant_has_property (NPObject * npobj, NPIdentifier name)
{
  NPFridaByteArray * self = reinterpret_cast<NPFridaByteArray *> (npobj);

  if (npfrida_nsfuncs->identifierisstring (name))
  {
    const gchar * property_name;

    property_name = static_cast<NPString *> (name)->UTF8Characters;
    if (strcmp (property_name, "length") == 0)
      return true;
  }
  else
  {
    int32_t index;

    index = npfrida_nsfuncs->intfromidentifier (name);
    if (index >= 0 && index < self->data_length)
      return true;
  }

  return false;
}

static bool
npfrida_variant_get_property (NPObject * npobj, NPIdentifier name, NPVariant * result)
{
  NPFridaByteArray * self = reinterpret_cast<NPFridaByteArray *> (npobj);

  if (npfrida_nsfuncs->identifierisstring (name))
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

    index = npfrida_nsfuncs->intfromidentifier (name);
    if (index >= 0 && index < self->data_length)
    {
      INT32_TO_NPVARIANT (self->data[index], *result);
      return true;
    }
  }

  npfrida_nsfuncs->setexception (npobj, "invalid property");
  return true;
}

static NPClass npfrida_variant_class =
{
  NP_CLASS_STRUCT_VERSION,
  npfrida_variant_allocate,
  npfrida_variant_deallocate,
  npfrida_variant_invalidate,
  npfrida_variant_has_method,
  npfrida_variant_invoke,
  npfrida_variant_invoke_default,
  npfrida_variant_has_property,
  npfrida_variant_get_property,
  NULL,
  NULL,
  NULL,
  NULL
};

NPObject *
npfrida_byte_array_new (NPP npp, const guint8 * data, gint data_length)
{
  NPFridaByteArray * obj;

  obj = reinterpret_cast<NPFridaByteArray *> (npfrida_nsfuncs->createobject (npp, &npfrida_variant_class));
  obj->data = (data_length != 0) ? static_cast<guint8 *> (g_memdup (data, data_length)) : NULL;
  obj->data_length = data_length;

  return &obj->np_object;
}

NPClass *
npfrida_variant_get_class (void)
{
  return &npfrida_variant_class;
}
