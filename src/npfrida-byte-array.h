#ifndef __NPFRIDA_BYTE_ARRAY_H__
#define __NPFRIDA_BYTE_ARRAY_H__

#include "npfrida-plugin.h"

G_BEGIN_DECLS

NPClass * npfrida_byte_array_get_class (void) G_GNUC_CONST;

NPObject * npfrida_byte_array_new (NPP npp, const guint8 * data, gint data_length);

G_END_DECLS

#endif
