
#include <glib.h>
#include <glib-object.h>
#include <gsf/gsf-output-memory.h>
#include <gsf/gsf-output-gzip.h>

#include "zfilter.h"

/* ---------------------------------------------------------------------- */

void *zfilter_new(void) {
    GsfOutputMemory *sink = GSF_OUTPUT_MEMORY(gsf_output_memory_new());
    return gsf_output_gzip_new(GSF_OUTPUT(sink), NULL);
}

int zfilter_write(void *obj, const void *data, size_t nbytes) {
    return gsf_output_write(GSF_OUTPUT(obj), nbytes, (guint8 *) data);
}

const void *zfilter_bytes(void *obj, size_t *nbytes) {
    GObject *gobj = G_OBJECT(obj), *sink;
    gsf_output_close(GSF_OUTPUT(gobj));

    g_object_get(gobj, "sink", &sink, NULL);
    *nbytes = gsf_output_size(GSF_OUTPUT(sink));

    const void *ret = gsf_output_memory_get_bytes(GSF_OUTPUT_MEMORY(sink));
    g_object_unref(sink); // g_object_get

    return ret;
}

void zfilter_del(void *obj) {
    GObject *gobj = G_OBJECT(obj), *sink;

    g_object_get(gobj, "sink", &sink, NULL);
    g_object_unref(gobj);
    g_object_unref(sink); // g_object_get
    g_object_unref(sink);
}
