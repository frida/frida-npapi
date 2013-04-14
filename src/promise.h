#ifndef __CLOUD_SPY_PROMISE_H__
#define __CLOUD_SPY_PROMISE_H__

#include "cloud-spy-plugin.h"

typedef struct _CloudSpyPromise CloudSpyPromise;
typedef gint CloudSpyPromiseResult;

enum _CloudSpyPromiseResult
{
  CLOUD_SPY_PROMISE_PENDING,
  CLOUD_SPY_PROMISE_SUCCESS,
  CLOUD_SPY_PROMISE_FAILURE
};

struct _CloudSpyPromise
{
  NPObject np_object;

  gpointer user_data;
  GDestroyNotify destroy_user_data;

  /*< private */
  NPP npp;
  GMutex * mutex;
  CloudSpyPromiseResult result;
  GArray * args;
  GPtrArray * on_success;
  GPtrArray * on_failure;
  GPtrArray * on_complete;
};

NPObject * cloud_spy_promise_new (NPP npp, gpointer user_data, GDestroyNotify destroy_user_data);

void cloud_spy_promise_resolve (CloudSpyPromise * self, const NPVariant * args, guint arg_count);
void cloud_spy_promise_reject (CloudSpyPromise * self, const NPVariant * args, guint arg_count);

#endif
