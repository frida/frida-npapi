#ifndef __CLOUD_SPY_OBJECT_H__
#define __CLOUD_SPY_OBJECT_H__

#include <glib-object.h>
#include <gio/gio.h>

#define CLOUD_SPY_TYPE_OBJECT (cloud_spy_object_get_type ())
#define CLOUD_SPY_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), CLOUD_SPY_TYPE_OBJECT, CloudSpyObject))
#define CLOUD_SPY_OBJECT_CAST(obj) ((CloudSpyObject *) (obj))
#define CLOUD_SPY_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), CLOUD_SPY_TYPE_OBJECT, CloudSpyObjectClass))
#define CLOUD_SPY_IS_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLOUD_SPY_TYPE_OBJECT))
#define CLOUD_SPY_IS_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CLOUD_SPY_TYPE_OBJECT))
#define CLOUD_SPY_OBJECT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), CLOUD_SPY_TYPE_OBJECT, CloudSpyObjectClass))

G_BEGIN_DECLS

typedef struct _CloudSpyObject           CloudSpyObject;
typedef struct _CloudSpyObjectClass      CloudSpyObjectClass;
typedef struct _CloudSpyObjectPrivate    CloudSpyObjectPrivate;

struct _CloudSpyObject
{
  GObject parent;

  CloudSpyObjectPrivate * priv;
};

struct _CloudSpyObjectClass
{
  GObjectClass parent_class;

  void (* destroy) (CloudSpyObject * self, GAsyncReadyCallback callback, gpointer user_data);
  void (* destroy_finish) (CloudSpyObject * self, GAsyncResult * res);
};

GType cloud_spy_object_get_type (void) G_GNUC_CONST;

GMainContext * cloud_spy_object_get_main_context (CloudSpyObject * self);

G_GNUC_INTERNAL void cloud_spy_object_type_init (void);
G_GNUC_INTERNAL void cloud_spy_object_type_deinit (void);
G_GNUC_INTERNAL gpointer cloud_spy_object_type_get_np_class (GType gtype);

G_END_DECLS

#endif
