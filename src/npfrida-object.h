#ifndef __NPFRIDA_OBJECT_H__
#define __NPFRIDA_OBJECT_H__

#include <glib-object.h>
#include <gio/gio.h>

#define NPFRIDA_TYPE_OBJECT (npfrida_object_get_type ())
#define NPFRIDA_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), NPFRIDA_TYPE_OBJECT, NPFridaObject))
#define NPFRIDA_OBJECT_CAST(obj) ((NPFridaObject *) (obj))
#define NPFRIDA_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), NPFRIDA_TYPE_OBJECT, NPFridaObjectClass))
#define NPFRIDA_IS_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NPFRIDA_TYPE_OBJECT))
#define NPFRIDA_IS_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NPFRIDA_TYPE_OBJECT))
#define NPFRIDA_OBJECT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), NPFRIDA_TYPE_OBJECT, NPFridaObjectClass))

G_BEGIN_DECLS

typedef struct _NPFridaObject           NPFridaObject;
typedef struct _NPFridaObjectClass      NPFridaObjectClass;
typedef struct _NPFridaObjectPrivate    NPFridaObjectPrivate;

struct _NPFridaObject
{
  GObject parent;

  NPFridaObjectPrivate * priv;
};

struct _NPFridaObjectClass
{
  GObjectClass parent_class;

  void (* destroy) (NPFridaObject * self, GAsyncReadyCallback callback, gpointer user_data);
  void (* destroy_finish) (NPFridaObject * self, GAsyncResult * res);
};

GType npfrida_object_get_type (void) G_GNUC_CONST;

GMainContext * npfrida_object_get_main_context (NPFridaObject * self);

G_GNUC_INTERNAL void npfrida_object_type_init (void);
G_GNUC_INTERNAL void npfrida_object_type_deinit (void);
G_GNUC_INTERNAL gpointer npfrida_object_type_get_np_class (GType gtype);

G_END_DECLS

#endif
