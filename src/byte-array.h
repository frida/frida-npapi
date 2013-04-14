#ifndef __CLOUD_SPY_BYTE_ARRAY_H__
#define __CLOUD_SPY_BYTE_ARRAY_H__

#include "cloud-spy-plugin.h"

G_BEGIN_DECLS

NPClass * cloud_spy_byte_array_get_class (void) G_GNUC_CONST;

NPObject * cloud_spy_byte_array_new (NPP npp, const guint8 * data, gint data_length);

G_END_DECLS

#endif
