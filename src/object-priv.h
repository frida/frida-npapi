#ifndef __NPFRIDA_OBJECT_PRIV_H__
#define __NPFRIDA_OBJECT_PRIV_H__

#include "npfrida-object.h"
#include "npfrida-plugin.h"

G_BEGIN_DECLS

typedef struct _NPFridaNPObject NPFridaNPObject;
typedef struct _NPFridaNPObjectClass NPFridaNPObjectClass;

struct _NPFridaNPObject
{
  NPObject np_object;
  NPFridaObject * g_object;
};

struct _NPFridaNPObjectClass
{
  NPClass np_class;
  GType g_type;
  NPFridaObjectClass * g_class;
};

G_GNUC_INTERNAL void npfrida_np_object_destroy (NPFridaNPObject * obj);

G_END_DECLS

#endif
