
#include <glib.h>
#include <glib-object.h>
#include <gsf/gsf-output-memory.h>
#include <gsf/gsf-output-gzip.h>

#include "zfilter.h"

void *zfilter_new(void)
{
    GsfOutputMemory *sink = GSF_OUTPUT_MEMORY(gsf_output_memory_new());
    return (void *) gsf_output_gzip_new(GSF_OUTPUT(sink), NULL);
}

int zfilter_write(void *obj, int nbytes, char *data)
{
    return gsf_output_write(GSF_OUTPUT(obj), nbytes, (guint8 *) data);
}

const char *zfilter_bytes(void *obj, int *nbytes)
{
    GObject *gobj = G_OBJECT(obj), *sink;
    gsf_output_close(GSF_OUTPUT(gobj));

    g_object_get(gobj, "sink", &sink, NULL);
    *nbytes = gsf_output_size(GSF_OUTPUT(sink));

    return (char *) gsf_output_memory_get_bytes(GSF_OUTPUT_MEMORY(sink));
}

void zfilter_del(void *obj)
{
    GObject *gobj = G_OBJECT(obj), *sink;

    g_object_get(gobj, "sink", &sink, NULL);
    g_object_unref(gobj);
    g_object_unref(sink);
}
