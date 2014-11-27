#ifndef __ZFILTER_H__
#define __ZFILTER_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------- */

    void *zfilter_new(void);

    int zfilter_write(void *obj, int num_bytes, char *data);
    const char *zfilter_bytes(void *obj, int *num_bytes);

    void zfilter_del(void *obj);

/* ---------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif
#endif
