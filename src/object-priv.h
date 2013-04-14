#ifndef __CLOUD_SPY_OBJECT_PRIV_H__
#define __CLOUD_SPY_OBJECT_PRIV_H__

#include "cloud-spy-object.h"
#include "cloud-spy-plugin.h"

G_BEGIN_DECLS

typedef struct _CloudSpyNPObject CloudSpyNPObject;
typedef struct _CloudSpyNPObjectClass CloudSpyNPObjectClass;

struct _CloudSpyNPObject
{
  NPObject np_object;
  CloudSpyObject * g_object;
};

struct _CloudSpyNPObjectClass
{
  NPClass np_class;
  GType g_type;
  CloudSpyObjectClass * g_class;
};

G_GNUC_INTERNAL void cloud_spy_np_object_destroy (CloudSpyNPObject * obj);

G_END_DECLS

#endif
