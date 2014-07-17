#ifndef __NPFRIDA_PROMISE_H__
#define __NPFRIDA_PROMISE_H__

#include "npfrida-plugin.h"

typedef struct _NPFridaPromise NPFridaPromise;
typedef gint NPFridaPromiseResult;

enum _NPFridaPromiseResult
{
  NPFRIDA_PROMISE_PENDING,
  NPFRIDA_PROMISE_SUCCESS,
  NPFRIDA_PROMISE_FAILURE
};

struct _NPFridaPromise
{
  NPObject np_object;

  gpointer user_data;
  GDestroyNotify destroy_user_data;

  /*< private */
  NPP npp;
  GMutex mutex;
  NPFridaPromiseResult result;
  GArray * args;
  GPtrArray * on_success;
  GPtrArray * on_failure;
  GPtrArray * on_complete;
};

NPObject * npfrida_promise_new (NPP npp, gpointer user_data, GDestroyNotify destroy_user_data);

void npfrida_promise_resolve (NPFridaPromise * self, const NPVariant * args, guint arg_count);
void npfrida_promise_reject (NPFridaPromise * self, const NPVariant * args, guint arg_count);

#endif
