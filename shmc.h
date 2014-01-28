#ifndef _SHMC_H_
#define _SHMC_H_

#include <stdint.h>
#include <pthread.h>

#define SHMC_VERSION 10101012

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { SHMC_OK, SHMC_NOTFOUND, SHMC_EXIST, SHMC_ESIZE, SHMC_ESPACE,
    SHMC_NOMEMORY, SHMC_ETOKEN, SHMC_ECREATE, SHMC_EVERSION, SHMC_SYSTEM } SHMC_RC;

typedef struct shmc_s           shmc_t;
typedef struct shmc_attr_s      shmc_attr_t;

typedef struct shmc_item_s      shmc_item_t;
typedef struct shmc_assoc_s     shmc_assoc_t;
typedef struct shmc_slab_s      shmc_slab_t;

uint32_t shmc_version();

/* if shmc_attr is null, attach to exists shmc
 * if shmc_attr is not null, create shmc
 */
SHMC_RC shmc_init(const char *token, shmc_attr_t *attr, shmc_t **shmc);
void shmc_destroy(shmc_t *shmc);
const char *shmc_error(SHMC_RC rc);

SHMC_RC shmc_get_nolock (shmc_t *shmc, const char *key, size_t nkey, char **val, size_t *nval, uint32_t *flags);
SHMC_RC shmc_getf_nolock(shmc_t *shmc, const char *key, size_t nkey, char *val,  size_t *nval, uint32_t *flags);

SHMC_RC shmc_set_nolock    (shmc_t *shmc, const char *key, size_t nkey, const char *val, size_t nval, uint32_t flags);
SHMC_RC shmc_add_nolock    (shmc_t *shmc, const char *key, size_t nkey, const char *val, size_t nval, uint32_t flags);
SHMC_RC shmc_replace_nolock(shmc_t *shmc, const char *key, size_t nkey, const char *val, size_t nval, uint32_t flags);
SHMC_RC shmc_prepend_nolock(shmc_t *shmc, const char *key, size_t nkey, const char *val, size_t nval, uint32_t flags);
SHMC_RC shmc_append_nolock (shmc_t *shmc, const char *key, size_t nkey, const char *val, size_t nval, uint32_t flags);

SHMC_RC shmc_incr_nolock(shmc_t *shmc, const char *key, size_t nkey, uint64_t val, uint64_t *new_val, uint32_t *flags);
SHMC_RC shmc_decr_nolock(shmc_t *shmc, const char *key, size_t nkey, uint64_t val, uint64_t *new_val, uint32_t *flags);

SHMC_RC shmc_del_nolock(shmc_t *shmc, const char *key, size_t nkey);

SHMC_RC shmc_dump_nolock(shmc_t *shmc, const char *file);
SHMC_RC shmc_load_nolock(shmc_t *shmc, const char *file);

void shmc_rdlock(shmc_t *shmc);
void shmc_wrlock(shmc_t *shmc);
void shmc_unlock(shmc_t *shmc);

static inline
SHMC_RC shmc_get(shmc_t *shmc, const char *key, size_t nkey, char **val, size_t *nval, uint32_t *flags) {
    shmc_rdlock(shmc);
    SHMC_RC rc = shmc_get_nolock(shmc, key, nkey, val, nval, flags);
    shmc_unlock(shmc);
    return rc;
}

static inline
SHMC_RC shmc_getf(shmc_t *shmc, const char *key, size_t nkey, char *val, size_t *nval, uint32_t *flags) {
    shmc_rdlock(shmc);
    SHMC_RC rc = shmc_getf_nolock(shmc, key, nkey, val, nval, flags);
    shmc_unlock(shmc);
    return rc;
}

static inline
SHMC_RC shmc_set(shmc_t *shmc, const char *key, size_t nkey, const char *val, size_t nval, uint32_t flags) {
    shmc_wrlock(shmc);
    SHMC_RC rc = shmc_set_nolock(shmc, key, nkey, val, nval, flags);
    shmc_unlock(shmc);
    return rc;
}

static inline
SHMC_RC shmc_add(shmc_t *shmc, const char *key, size_t nkey, const char *val, size_t nval, uint32_t flags) {
    shmc_wrlock(shmc);
    SHMC_RC rc = shmc_add_nolock(shmc, key, nkey, val, nval, flags);
    shmc_unlock(shmc);
    return rc;
}

static inline
SHMC_RC shmc_replace(shmc_t *shmc, const char *key, size_t nkey, const char *val, size_t nval, uint32_t flags) {
    shmc_wrlock(shmc);
    SHMC_RC rc = shmc_replace_nolock(shmc, key, nkey, val, nval, flags);
    shmc_unlock(shmc);
    return rc;
}

static inline
SHMC_RC shmc_prepend(shmc_t *shmc, const char *key, size_t nkey, const char *val, size_t nval, uint32_t flags) {
    shmc_wrlock(shmc);
    SHMC_RC rc = shmc_prepend_nolock(shmc, key, nkey, val, nval, flags);
    shmc_unlock(shmc);
    return rc;
}
static inline
SHMC_RC shmc_append(shmc_t *shmc, const char *key, size_t nkey, const char *val, size_t nval, uint32_t flags) {
    shmc_wrlock(shmc);
    SHMC_RC rc = shmc_append_nolock(shmc, key, nkey, val, nval, flags);
    shmc_unlock(shmc);
    return rc;
}

static inline
SHMC_RC shmc_incr(shmc_t *shmc, const char *key, size_t nkey, uint64_t val, uint64_t *new_val, uint32_t *flags) {
    shmc_wrlock(shmc);
    SHMC_RC rc = shmc_incr_nolock(shmc, key, nkey, val, new_val, flags);
    shmc_unlock(shmc);
    return rc;
}

static inline
SHMC_RC shmc_decr(shmc_t *shmc, const char *key, size_t nkey, uint64_t val, uint64_t *new_val, uint32_t *flags) {
    shmc_wrlock(shmc);
    SHMC_RC rc = shmc_decr_nolock(shmc, key, nkey, val, new_val, flags);
    shmc_unlock(shmc);
    return rc;
}

static inline SHMC_RC shmc_del(shmc_t *shmc, const char *key, size_t nkey) {
    shmc_wrlock(shmc);
    SHMC_RC rc = shmc_del_nolock(shmc, key, nkey);
    shmc_unlock(shmc);
    return rc;
}

static inline SHMC_RC shmc_dump(shmc_t *shmc, const char *file) {
    shmc_wrlock(shmc);
    SHMC_RC rc = shmc_dump_nolock(shmc, file);
    shmc_unlock(shmc);
    return rc;
}

static inline SHMC_RC shmc_load(shmc_t *shmc, const char *file) {
    shmc_wrlock(shmc);
    SHMC_RC rc = shmc_load_nolock(shmc, file);
    shmc_unlock(shmc);
    return rc;
}

struct shmc_s {
    /* fix addr in share memory */
    unsigned int     *version;
	shmc_attr_t      *attr;
    /* pthread lock */
    pthread_rwlock_t *lock;
    pthread_mutex_t  *mutex;
	shmc_item_t     **heads;
	shmc_item_t     **tails;
	shmc_item_t     **buckets;
	shmc_slab_t      *slabs;
    void             *raw;

    /* file lock */ 
    int               fd;
};

struct shmc_attr_s {
    /* read only after startup */
    size_t mem_limit;
	int nbuckets;
	int mode;

    size_t item_size_min;
    size_t item_size_max;
    float item_size_factor;

    int evict_to_free;
	int default_counter;
    int use_flock;

    /* runtime info, read only for user */
    size_t mem_used;
    int slabs_count;
	int max_depth;
    size_t nitems; 
};

#define shmc_attr_set_mem_limit(attr, limit) \
    (attr)->mem_limit = (limit)

#define shmc_attr_set_nbuckets(attr, n) \
    (attr)->nbuckets = (n)

#define shmc_attr_set_mode(attr, m) \
	(attr)->mode = (m)

#define shmc_attr_set_item_size_min(attr, min) \
    (attr)->item_size_min = (min)

#define shmc_attr_set_item_size_max(attr, max) \
    (attr)->item_size_max = (max)

#define shmc_attr_set_item_size_factor(attr, factor) \
    (attr)->item_size_factor = (factor)

#define shmc_attr_set_evict_to_free(attr, on_off) \
	(attr)->evict_to_free = (on_off)

#define shmc_attr_set_default_counter(attr, on_off) \
    (attr)->default_counter = (on_off)

/* do not use_flock in mulit thread */
#define shmc_attr_use_flock(attr, on_off) \
	(attr)->use_flock = (on_off)

#define SHMC_ATTR_INITIALIZER     \
 { 64 * 1024 * 1024, 65536, 0644, \
   64, 1024 * 1024, 2,            \
   1, 1, 0,                       \
   0, 0, 0, 0 }

#ifdef __cplusplus
}
#endif

#endif
