#ifndef __ZFILTER_H__
#define __ZFILTER_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------- */

void *zfilter_new(void);

int zfilter_write(void *obj, const void *data, size_t nbytes);

const void *zfilter_bytes(void *obj, size_t *num_bytes);

void zfilter_del(void *obj);

/* ---------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif
#endif
