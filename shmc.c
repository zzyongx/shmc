#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <inttypes.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include <hash.h>
#include <shmc.h>

/* make sure the .h and .so are the same version */
uint32_t shmc_version() {
    static const uint32_t version = SHMC_VERSION;
    return version;
}

struct shmc_item_s {
	shmc_item_t *next;
	shmc_item_t *prev;
	shmc_item_t *h_next;

    int          clsid;

    uint32_t     flags;
    char        *key;
	size_t       nkey;
    char        *val;
	size_t       nval;
    void        *end[];
};

struct shmc_slab_s {
    shmc_item_t *free_item;
    size_t       size;
    size_t       count;
};

#ifdef SHMC_VERBOSE
# define a2r(shmc, p) printf("%04d a %p to r %p\n", __LINE__, (void *)(p), \
        ((p) ? (void *) ((void *)(p) - (void *)((shmc)->version)) : (p))),
# define r2a(shmc, p) printf("%04d r %p to a %p\n", __LINE__, (void *)(p), \
        ((p) ? (void *) ((void *)((shmc)->version) + (size_t)(p)) : (p))),
# define shmc_debug(fmt, arg...) printf(fmt, ##arg)
#else
# define a2r(shmc, p)
# define r2a(shmc, p)
# define shmc_debug(fmt, arg...)
#endif


#ifdef SHMC_FAST
# define A2R(shmc, p) (p)
# define R2A(shmc, p, type) (p) 
#else
# define A2R(shmc, p) (a2r(shmc, p) ((p) ? (void *) ((void *)(p) - (void *)((shmc)->version)) : (p)))
# define R2A(shmc, p, type) (r2a(shmc, p) ((p) ? (type *)((void *)((shmc)->version) + (size_t)(p)) : (p)))
#endif

#define item_size_ok(shmc, nkey, nval) \
    ((sizeof(shmc_item_t) + (nkey) + (nval)) < ((shmc)->attr->item_size_max))

static shmc_item_t *assoc_find(shmc_t *shmc, const char *key, size_t nkey);
static void assoc_insert(shmc_t *shmc, const char *key, size_t nkey, shmc_item_t *item);
static void assoc_delete(shmc_t *shmc, const char *key, size_t nkey);        

static void item_link(shmc_t *shmc, shmc_item_t *item);
static void item_unlink(shmc_t *shmc, shmc_item_t *item);
static void item_relink(shmc_t *shmc, shmc_item_t *item);

static shmc_item_t *item_alloc(shmc_t *shmc, size_t nkey, size_t nval);
static void item_free(shmc_t *shmc, shmc_item_t *item);

static size_t size_of_mmap(const shmc_attr_t *attr, const int slabs_count)
{
    size_t size = 0;

    /* version */
    size += sizeof(uint32_t);    

    /* shmc attribute */
    size += sizeof(shmc_attr_t);

    /* shmc rwlock */
    size += sizeof(pthread_rwlock_t);
    size += sizeof(pthread_mutex_t);

    /* LRU list */
    size += sizeof(shmc_item_t *) * slabs_count;
    size += sizeof(shmc_item_t *) * slabs_count;

    /* assoc */
    size += sizeof(shmc_item_t *) * attr->nbuckets;

    /* slabs */
    size += sizeof(shmc_slab_t) * slabs_count;

    /* raw memory */
    size += attr->mem_limit;

    return size;
}

static void format_mmap(shmc_t *shmc, void *raw, const int nbuckets, const int slabs_count)
{
    /* version */
    shmc->version = raw;

    /* shmc attribute */
    shmc->attr = (void *) shmc->version + sizeof(uint32_t);

    /* shmc rwlock */
    shmc->lock  = (void *) shmc->attr + sizeof(shmc_attr_t);
    shmc->mutex = (void *) shmc->lock + sizeof(pthread_rwlock_t);

    /* LRU list */
    shmc->heads = (void *) shmc->mutex + sizeof(pthread_mutex_t);
    shmc->tails = (void *) shmc->heads + sizeof(shmc_item_t *) * slabs_count;

    /* assoc */
    shmc->buckets = (void *) shmc->tails + sizeof(shmc_item_t *) * slabs_count;

    /* slabs */
    shmc->slabs = (void *) shmc->buckets + sizeof(shmc_item_t *) * nbuckets;

    /* raw memory */
    shmc->raw = (void *) shmc->slabs + sizeof(shmc_slab_t) * slabs_count;
}

#define ALIGN_BYTES 8
#define align_size(size) (((size) % ALIGN_BYTES) ? \
   (size) + ALIGN_BYTES - (size % ALIGN_BYTES) : (size))

/* size   2       4       8       16
 * clsid  0       1       2       3
 * layout [x, 2), [2, 4), [4, 8), [8, 16)
 */
static int item_clsid(const shmc_t *shmc, size_t nkey, size_t nval)
{
    size_t size = sizeof(shmc_item_t) + nkey + nval;

    int id = 0;
    while (size > shmc->slabs[id].size) {
        id++; 
    }
    return id;
}

static int count_of_slabs(const shmc_attr_t *attr)
{
    size_t size = sizeof(shmc_item_t) + attr->item_size_min;

    int count = 0;
    while (size < attr->item_size_max) {
        size = align_size(size);
        size *= attr->item_size_factor;
        count++;
    }
    return count;
}

static void format_slabs(shmc_t *shmc, const int slabs_count)
{
    size_t size = sizeof(shmc_item_t) + shmc->attr->item_size_min;

    shmc_slab_t *slabs = shmc->slabs;
    int id;
    for (id = 0; id < slabs_count; ++id) {
        size = align_size(size);
        slabs[id].size = size;
        slabs[id].count = shmc->attr->item_size_max / size;

        size_t len = slabs[id].size * slabs[id].count;

        slabs[id].free_item = 0; 
        void *raw = shmc->raw + shmc->attr->mem_used;
        shmc->attr->mem_used += len;

        assert(shmc->attr->mem_used < shmc->attr->mem_limit);

        size_t i;
        for (i = 0; i < slabs[id].count; ++i) {
            shmc_item_t *item = raw;
            item->next = slabs[id].free_item;
            slabs[id].free_item = A2R(shmc, item);
            raw += slabs[id].size;
            shmc_debug("slabs[%02d] add    %p, next %p\n", id, slabs[id].free_item, item->next);
        }

        size *= shmc->attr->item_size_factor;
    }
}

static SHMC_RC mmap_create(shmc_t *shmc, const char *token, const shmc_attr_t *attr)
{
    /* O_CREAT | O_EXCL ensure only one process can create 
     * if token is exist, just mmap_attach
     * or delete the token, and try again
     */
    mode_t mask = umask(0);
    shmc->fd = open(token, O_RDWR | O_CREAT | O_EXCL, attr->mode);
    umask(mask);
    if (shmc->fd == -1) {
        if (errno == EEXIST) return SHMC_ECREATE;
        else return SHMC_SYSTEM;
    }

    const int slabs_count = count_of_slabs(attr);
    size_t size = size_of_mmap(attr, slabs_count);

    /* truncate file to mmap size */
    if (ftruncate(shmc->fd, size) == -1) return SHMC_SYSTEM;

    void *raw = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, shmc->fd, 0);
    if (raw == MAP_FAILED) return SHMC_SYSTEM;

    format_mmap(shmc, raw, attr->nbuckets, slabs_count);

    *(shmc->version) = SHMC_VERSION;
    memcpy(shmc->attr, attr, sizeof(shmc_attr_t));
    shmc->attr->slabs_count = slabs_count;

    /* lock subsystem */
    /* always init pthread lock */
    pthread_rwlockattr_t lock_attr;
    pthread_rwlockattr_init(&lock_attr); 
    pthread_rwlockattr_setpshared(&lock_attr, PTHREAD_PROCESS_SHARED);
    pthread_rwlock_init(shmc->lock, &lock_attr);
    pthread_rwlockattr_destroy(&lock_attr);

    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(shmc->mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    /* LRU list */
    memset(shmc->heads, 0x00, sizeof(shmc_item_t *) * shmc->attr->slabs_count);
    memset(shmc->tails, 0x00, sizeof(shmc_item_t *) * shmc->attr->slabs_count);

    /* assoc subsystem */
    memset(shmc->buckets, 0x00, sizeof(shmc_item_t *) * shmc->attr->nbuckets);

    /* slabs subsystem */
    format_slabs(shmc, slabs_count);

    return SHMC_OK;
}

static SHMC_RC mmap_attach(shmc_t *shmc, const char *token)
{
    shmc->fd = open(token, O_RDWR);
    if (shmc->fd == -1) {
        /* if no one have call mmap_create */
        if (errno == ENOENT) return SHMC_ETOKEN;
        else return SHMC_SYSTEM;
    }

    /* first map get the attr */
    size_t size = sizeof(uint32_t) + sizeof(shmc_attr_t);
    void *raw = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, shmc->fd, 0);
    if (raw == MAP_FAILED) return SHMC_SYSTEM;

    uint32_t version = *(uint32_t *)raw;
    /* check the lib's version */
    if (version != SHMC_VERSION) {
        munmap(raw, size);
        return SHMC_EVERSION;
    }

    /* get the mmap's size */
    shmc->attr = raw + sizeof(uint32_t);
    const int slabs_count = count_of_slabs(shmc->attr);
    const int nbuckets = shmc->attr->nbuckets;
    size_t total_size = size_of_mmap(shmc->attr, slabs_count);

    /* munmap */
    munmap(raw, size);

    /* remmap the total space */
    raw = mmap(0, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shmc->fd, 0);
    if (raw == MAP_FAILED) return SHMC_SYSTEM;

    format_mmap(shmc, raw, nbuckets, slabs_count); 

    return SHMC_OK;
}

SHMC_RC shmc_init(const char *token, shmc_attr_t *attr, shmc_t **shmc)
{
    *shmc = malloc(sizeof(shmc_t));
    if (!*shmc) return SHMC_SYSTEM;

    /* init runtime attr, fix invalid attr */
    if (attr) {
        attr->mem_used = 0;
        attr->slabs_count = 0;
        attr->max_depth = 0;
        attr->nitems = 0;

        if (attr->item_size_factor <= 1.5) {
            attr->item_size_factor = 1.5; 
        }
    }

    SHMC_RC rc;

    /* mmap subsystem */
    if (attr) {
        rc = mmap_create(*shmc, token, attr);
    } else {
        rc = mmap_attach(*shmc, token);
    }
    if (rc != SHMC_OK) goto destroy;

    return rc;

destroy:
    /* lock will be released when close fd if necessary */
    if ((*shmc)->fd != -1) close((*shmc)->fd);
    free(*shmc);
    return rc;
}

void shmc_destroy(shmc_t *shmc)
{
    shmc_wrlock(shmc);
    size_t size = size_of_mmap(shmc->attr, shmc->attr->slabs_count);
    shmc_unlock(shmc);

    /* never destroy pthread lock
     * pthread_rwlock_destroy(shmc->lock);
     */
    /* msync with MS_SYNC, ensure the data sync to the disk */
    msync((void *) shmc->version, size, MS_SYNC);

    munmap((void *) shmc->version, size);
    close(shmc->fd);

    free(shmc);
}

const char *shmc_error(SHMC_RC rc)
{
    const char *error;
    switch (rc) {
        case SHMC_OK: error = "success"; break;
        case SHMC_NOTFOUND: error = "key not found"; break; 
        case SHMC_EXIST: error = "key already exit"; break;
        case SHMC_ESIZE: error = "too large object"; break;
        case SHMC_ESPACE: error = "user provided space error"; break;
        case SHMC_NOMEMORY: error = "out of memory"; break;
        case SHMC_ETOKEN: error = "token not exist"; break;
        case SHMC_ECREATE: error = "shmc already created"; break;
        case SHMC_EVERSION: error = "shmc version conflict"; break;
        case SHMC_SYSTEM: error = strerror(errno); break;
        default: error = "unknow shmc error"; break;
    }
    return error;
}

SHMC_RC shmc_get_nolock(shmc_t *shmc, const char *key, size_t nkey, char **val, size_t *nval, uint32_t *flags)
{
    shmc_item_t *item = assoc_find(shmc, key, nkey);
    if (!item) return SHMC_NOTFOUND;

    pthread_mutex_lock(shmc->mutex);
    item_relink(shmc, item);
    pthread_mutex_unlock(shmc->mutex);

    *val = malloc(item->nval);
    if (*val) {
        memcpy(*val, R2A(shmc, item->val, char), item->nval);
        *nval = item->nval;
        if (flags) *flags = item->flags;
        return SHMC_OK;
    } else {
        return SHMC_SYSTEM;
    }
}

SHMC_RC shmc_getf_nolock(shmc_t *shmc, const char *key, size_t nkey, char *val, size_t *nval, uint32_t *flags)
{
    shmc_item_t *item = assoc_find(shmc, key, nkey);
    if (!item) return SHMC_NOTFOUND;

    pthread_mutex_lock(shmc->mutex);
    item_relink(shmc, item);
    pthread_mutex_unlock(shmc->mutex);

    if (*nval >= item->nval) {
        memcpy(val, R2A(shmc, item->val, char), item->nval); 
        *nval = item->nval;
        if (flags) *flags = item->flags;
        return SHMC_OK;
    } else {
        return SHMC_ESPACE;
    }
}

SHMC_RC shmc_set_nolock(shmc_t *shmc, const char *key, size_t nkey, const char *val, size_t nval, uint32_t flags)
{
    if (!item_size_ok(shmc, nkey, nval)) return SHMC_ESIZE;

    /* delete first */
    shmc_del_nolock(shmc, key, nkey);

    shmc_item_t *item = item_alloc(shmc, nkey, nval);
    if (!item) return SHMC_NOMEMORY;

    assoc_insert(shmc, key, nkey, item);
    item_link(shmc, item);

    item->flags = flags;
    memcpy(R2A(shmc, item->key, char), key, nkey);
    memcpy(R2A(shmc, item->val, char), val, nval);

    return SHMC_OK;
}

SHMC_RC shmc_add_nolock(shmc_t *shmc, const char *key, size_t nkey, const char *val, size_t nval, uint32_t flags)
{
    shmc_item_t *item = assoc_find(shmc, key, nkey);
    if (item) return SHMC_EXIST;

    return shmc_set_nolock(shmc, key, nkey, val, nval, flags);
}

SHMC_RC shmc_replace_nolock(shmc_t *shmc, const char *key, size_t nkey, const char *val, size_t nval, uint32_t flags)
{
    shmc_item_t *item = assoc_find(shmc, key, nkey);
    if (!item) return SHMC_NOTFOUND;

    if (item->clsid == item_clsid(shmc, nkey, nval)) {
        item_relink(shmc, item);
        item->flags = flags;
        memcpy(R2A(shmc, item->val, char), val, nval);
        item->nval = nval;
        return SHMC_OK;
    }

    assoc_delete(shmc, key, nkey);
    item_unlink(shmc, item);
    item_free(shmc, item);

    return shmc_set_nolock(shmc, key, nkey, val, nval, flags);
}

SHMC_RC shmc_prepend_nolock(shmc_t *shmc, const char *key, size_t nkey, const char *val, size_t nval, uint32_t flags)
{
    shmc_item_t *item = assoc_find(shmc, key, nkey);
    if (!item) return SHMC_NOTFOUND;

    if (item->clsid == item_clsid(shmc, nkey, (nval + item->nval))) {
        item_relink(shmc, item);
        item->flags = flags;
        memmove(R2A(shmc, item->val, char) + nval, R2A(shmc, item->val, char), item->nval);
        memcpy(R2A(shmc, item->val, char), val, nval);
        item->nval += nval;
        return SHMC_OK;
    }

    if (!item_size_ok(shmc, nkey, nval + item->nval)) return SHMC_ESIZE;

    shmc_item_t *item_new = item_alloc(shmc, nkey, nval + item->nval);
    if (!item_new) return SHMC_NOMEMORY;

    assoc_delete(shmc, key, nkey);
    item_unlink(shmc, item);

    assoc_insert(shmc, key, nkey, item_new);
    item_link(shmc, item_new);

    item->flags = flags;
    memcpy(R2A(shmc, item_new->key, char), key, nkey);
    memcpy(R2A(shmc, item_new->val, char), val, nval);
    memcpy(R2A(shmc, item_new->val, char) + nval, R2A(shmc, item->val, char), item->nval);

    item_free(shmc, item);

    return SHMC_OK;
}

SHMC_RC shmc_append_nolock(shmc_t *shmc, const char *key, size_t nkey, const char *val, size_t nval, uint32_t flags)
{
    shmc_item_t *item = assoc_find(shmc, key, nkey);
    if (!item) return SHMC_NOTFOUND;

    if (item->clsid == item_clsid(shmc, nkey, (nval + item->nval))) {
        item_relink(shmc, item);
        item->flags = flags;
        memcpy(R2A(shmc, item->val, char) + item->nval, val, nval);
        item->nval += nval;
        return SHMC_OK;
    }

    if (!item_size_ok(shmc, nkey, nval + item->nval)) return SHMC_ESIZE;

    shmc_item_t *item_new = item_alloc(shmc, nkey, nval + item->nval);
    if (!item_new) return SHMC_NOMEMORY;

    assoc_delete(shmc, key, nkey);
    item_unlink(shmc, item);

    assoc_insert(shmc, key, nkey, item_new);
    item_link(shmc, item_new);

    item->flags = flags;
    memcpy(R2A(shmc, item_new->key, char), key, nkey);
    memcpy(R2A(shmc, item_new->val, char), R2A(shmc, item->val, char), item->nval);
    memcpy(R2A(shmc, item_new->val, char) + item->nval, val, nval);

    item_free(shmc, item);

    return SHMC_OK;
}

#define UINT64_SIZE sizeof("18446744073709551616")
static uint64_t safe_strtoull(const char *val, size_t nval)
{
    size_t i;
    uint64_t value = 0;

    if (nval > UINT64_SIZE-1) nval = UINT64_SIZE-1;

    for (i = 0; i < nval; ++i) {
        if (val[i] < '0' || val[i] > '9') break;
        value = value * 10 + (val[i] - '0');
    }
    return value;
}

static SHMC_RC shmc_arithmetic(shmc_t *shmc, const char *key, size_t nkey, uint64_t val, int incr,
        uint64_t *new_val, uint32_t *flags)
{
    uint64_t old_val;
    uint32_t old_flags = 0;
    shmc_item_t *new_item;
    shmc_item_t *old_item;
    
    old_item = assoc_find(shmc, key, nkey);

    if (old_item) {
        old_val = safe_strtoull(R2A(shmc, old_item->val, char), old_item->nval);
        old_flags = old_item->flags;

        if (old_item->nval == UINT64_SIZE) {
            new_item = old_item;
        } else {
            /* if old item is not digit */
            new_item = item_alloc(shmc, nkey, UINT64_SIZE);
            if (new_item) {
                /* if allow new success, delete old */
                assoc_delete(shmc, key, nkey);
                item_unlink(shmc, old_item);
                item_free(shmc, old_item);
            }
        }
    } else {
        if (shmc->attr->default_counter) {
            old_val = 0;
            new_item = item_alloc(shmc, nkey, UINT64_SIZE);
        } else {
            return SHMC_NOTFOUND;
        }
    }

    if (!new_item) return SHMC_NOMEMORY;

    /* if new item, initialize */
    if (new_item != old_item) {
        assoc_insert(shmc, key, nkey, new_item);
        item_link(shmc, new_item);

        new_item->flags = old_flags; 
        memcpy(R2A(shmc, new_item->key, char), key, nkey);
        memset(R2A(shmc, new_item->val, char), ' ', UINT64_SIZE);

        if (flags) new_item->flags = *flags;
    }

    if (flags) *flags = new_item->flags;

    if (incr) {
        *new_val = old_val + val; 
    } else {
        if (old_val < val) {
            *new_val = 0;
        } else {
            *new_val = old_val - val;
        }
    }
    sprintf(R2A(shmc, new_item->val, char), "%"PRIu64, *new_val);

    return SHMC_OK;
}

SHMC_RC shmc_incr_nolock(shmc_t *shmc, const char *key, size_t nkey, uint64_t val, uint64_t *new_val, uint32_t *flags)
{
    return shmc_arithmetic(shmc, key, nkey, val, 1, new_val, flags);
}

SHMC_RC shmc_decr_nolock(shmc_t *shmc, const char *key, size_t nkey, uint64_t val, uint64_t *new_val, uint32_t *flags)
{
    return shmc_arithmetic(shmc, key, nkey, val, 0, new_val, flags);
}

SHMC_RC shmc_del_nolock(shmc_t *shmc, const char *key, size_t nkey)
{
    shmc_item_t *item = assoc_find(shmc, key, nkey);
    if (!item) return SHMC_NOTFOUND;

    assoc_delete(shmc, key, nkey);
    item_unlink(shmc, item);

    item_free(shmc, item);
    return SHMC_OK;
}

SHMC_RC shmc_dump_nolock(shmc_t *shmc, const char *file)
{
    FILE *fp = fopen(file, "w");
    if (!fp) {
        return SHMC_SYSTEM; 
    }

    int i;
    shmc_item_t *item, *next;
    for (i = 0; i < shmc->attr->slabs_count; ++i) {
        for (item = shmc->heads[i]; item; item = next) {
            shmc_item_t *it = R2A(shmc, item, shmc_item_t);
            fprintf(fp, "%d %d %.*s %.*s\n", (int) it->nkey, (int) it->nval,
                    (int) it->nkey, R2A(shmc, it->key, char), (int) it->nval, R2A(shmc, it->val, char));
            next = it->next;
        }
    }

    fclose(fp);
    return SHMC_OK;
}

SHMC_RC shmc_load_nolock(shmc_t *shmc, const char *file)
{
    FILE *fp = fopen(file, "r");
    if (!fp) {
        return SHMC_SYSTEM;
    }

    const size_t BUFFER_SIZE = 1024 * 1024 + 1024;
    char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        fclose(fp);
        return SHMC_SYSTEM;
    }

    SHMC_RC rc = SHMC_OK;

    size_t nbuffer, offset = 0;
    while ((nbuffer = fread(buffer + offset, 1, BUFFER_SIZE - offset, fp))) {
        nbuffer += offset;
        offset = 0;
        while (rc == SHMC_OK) {
            /* nkey nval key val\n */
            char *nkeyp = buffer + offset;

            char *nvalp = memchr(nkeyp, ' ', nbuffer - (nkeyp - buffer));
            if (!nvalp || nvalp + 1 == buffer + nbuffer) break;

            *nvalp = '\0';
            size_t nkey = atoi(nkeyp);
            *nvalp = ' ';
            nvalp += 1;

            char *key = memchr(nvalp, ' ', nbuffer - (nvalp - buffer));
            if (!key || key + 1 == buffer + nbuffer) break;

            *key = '\0';
            size_t nval = atoi(nvalp);
            *key = ' ';
            key += 1;

            if (key + nkey + 1 + nval + 1 > buffer + nbuffer) break;
            char *val = key + nkey + 1;

            rc = shmc_set_nolock(shmc, key, nkey, val, nval, 0);
            if (rc != SHMC_OK) break;

            offset = (key + nkey + 1 + nval + 1) - buffer;
        }

        if (rc == SHMC_OK) {
            if (offset == 0) {
                rc = SHMC_ESIZE;
                break;
            } else if (nbuffer != offset) {
                memmove(buffer, buffer + offset, nbuffer - offset);
                offset = nbuffer - offset;
            }
        } else {
            break;
        }
    }

    free(buffer);
    fclose(fp);
    return rc;
}

static void shmc_fcntl(shmc_t *shmc, int type)
{
    struct flock lock;
    lock.l_type   = type;
    lock.l_start  = 0;
    lock.l_whence = SEEK_SET;
    lock.l_len    = 0;

    fcntl(shmc->fd, F_SETLKW, &lock);
}

void shmc_rdlock(shmc_t *shmc)
{
    if (shmc->attr->use_flock) {
        shmc_fcntl(shmc, F_RDLCK);
    } else {
        pthread_rwlock_rdlock(shmc->lock); 
    }
    shmc_debug("enter read lock\n");
}

void shmc_wrlock(shmc_t *shmc)
{
    if (shmc->attr->use_flock) {
        shmc_fcntl(shmc, F_WRLCK);
    } else {
        pthread_rwlock_wrlock(shmc->lock); 
    }
    shmc_debug("enter write lock\n");
}

void shmc_unlock(shmc_t *shmc)
{
    if (shmc->attr->use_flock) {
        shmc_fcntl(shmc, F_UNLCK);
    } else {
        pthread_rwlock_unlock(shmc->lock); 
    }
    shmc_debug("leave lock\n");
}

static void item_link(shmc_t *shmc, shmc_item_t *item)
{
    shmc_item_t **head = &shmc->heads[item->clsid];
    shmc_item_t **tail = &shmc->tails[item->clsid];

    item->prev = 0;
    item->next = *head;
    if (item->next) R2A(shmc, item->next, shmc_item_t)->prev = A2R(shmc, item);
    *head = A2R(shmc, item);
    if (*tail == 0) *tail = A2R(shmc, item);

    shmc_debug("heads[%02d] head %p, next %p\n", item->clsid, *head, item->next);
    shmc_debug("tails[%02d] tail %p, prev %p\n", item->clsid, *tail, item->prev);
}

static void item_unlink(shmc_t *shmc, shmc_item_t *item)
{
    shmc_item_t **head = &shmc->heads[item->clsid];
    shmc_item_t **tail = &shmc->tails[item->clsid];

    if (*head == A2R(shmc, item)) {
        *head = item->next;
    }
    if (*tail == A2R(shmc, item)) {
        *tail = item->prev;
    }

    if (item->next) R2A(shmc, item->next, shmc_item_t)->prev = item->prev;
    if (item->prev) R2A(shmc, item->prev, shmc_item_t)->next = item->next;

    shmc_debug("heads[%02d] head %p, next %p\n", item->clsid, *head, item->next);
    shmc_debug("tails[%02d] tail %p, prev %p\n", item->clsid, *tail, item->prev);
}

static void item_relink(shmc_t *shmc, shmc_item_t *item)
{
    item_unlink(shmc, item);
    item_link(shmc, item);
}

static shmc_item_t *assoc_find(shmc_t *shmc, const char *key, size_t nkey)
{
    int depth = 0;
    uint32_t hv = hash(key, nkey, 0);
    shmc_item_t *item = R2A(shmc, shmc->buckets[hv % shmc->attr->nbuckets], shmc_item_t);
    while (item) {
        if (++depth > shmc->attr->max_depth) {
            shmc->attr->max_depth = depth;
        }
        shmc_debug("assoc[%d]_find item %p item->h_next %p\n", hv % shmc->attr->nbuckets, item, item->h_next);
        if (item->nkey == nkey && memcmp(key, R2A(shmc, item->key, char), nkey) == 0) {
            return item; 
        } 
        item = R2A(shmc, item->h_next, shmc_item_t);
    }
    return 0;
}

static void assoc_insert(shmc_t *shmc, const char *key, size_t nkey, shmc_item_t *item)
{
    shmc->attr->nitems++;
    uint32_t hv = hash(key, nkey, 0);
    uint32_t slot = hv % shmc->attr->nbuckets;
    item->h_next = shmc->buckets[slot]; /* both of them are R addr */
    shmc->buckets[slot] = A2R(shmc, item);
    shmc_debug("assoc[%d]_insert item %p item->h_next %p\n", (int) slot, item, item->h_next);
}

static void assoc_delete(shmc_t *shmc, const char *key, size_t nkey)
{
    uint32_t hv = hash(key, nkey, 0);
    uint32_t slot = hv % shmc->attr->nbuckets;
    shmc_item_t **item = &shmc->buckets[slot];

    while (R2A(shmc, *item, shmc_item_t)) {
        if (R2A(shmc, *item, shmc_item_t)->nkey == nkey &&
                memcmp(key, R2A(shmc, R2A(shmc, *item, shmc_item_t)->key, char), nkey) == 0) {
            assert(shmc->attr->nitems);
            shmc->attr->nitems--;
            shmc_item_t *nxt = R2A(shmc, *item, shmc_item_t)->h_next;
            shmc_debug("assoc[%d]_delete item %p item->h_next %p\n", slot, *item, nxt);
            R2A(shmc, *item, shmc_item_t)->h_next = 0;
            *item = nxt;  /* both of them are R addr */
            break;
        } 
        item = &(R2A(shmc, *item, shmc_item_t)->h_next); 
    }
}

static shmc_item_t *item_alloc(shmc_t *shmc, size_t nkey, size_t nval)
{
    assert(item_size_ok(shmc, nkey, nval));

    shmc_item_t *item = 0;

    /* find slot */
    int id = item_clsid(shmc, nkey, nval);
    shmc_slab_t *slabs = shmc->slabs;

    /* alloc from slabs */
    if (slabs[id].free_item) {
        item = R2A(shmc, slabs[id].free_item, shmc_item_t);
        slabs[id].free_item = item->next;  /* both of them are R addr */
        shmc_debug("slabs[%02d] remove %p, next %p\n", id, A2R(shmc, item), slabs[id].free_item);
    }

    if (!item) {
        /* alloc from mem pool */
        size_t len = slabs[id].size * slabs[id].count;
        if (shmc->attr->mem_used + len < shmc->attr->mem_limit) {
            void *raw = shmc->raw + shmc->attr->mem_used;
            shmc->attr->mem_used += len;

            size_t i;
            for (i = 0; i < slabs[id].count; ++i) {
                shmc_item_t *item = raw;
                item->next = slabs[id].free_item;
                slabs[id].free_item = A2R(shmc, item);
                raw += slabs[id].size;
                shmc_debug("slabs[%02d] add    %p, next %p\n", id, slabs[id].free_item, item->next);
            }
        } else {
            /* LRU */
            if (shmc->attr->evict_to_free) {
                shmc_item_t *tail = R2A(shmc, shmc->tails[id], shmc_item_t);
                if (tail) {
                    assoc_delete(shmc, R2A(shmc, tail->key, char), tail->nkey);
                    item_unlink(shmc, tail);
                    item_free(shmc, tail);
                }
            }
        }

        /* try again */
        if (slabs[id].free_item) {
            item = R2A(shmc, slabs[id].free_item, shmc_item_t); 
            slabs[id].free_item = item->next; /* both of them are R addr */
            shmc_debug("slabs[%02d] remove %p, next %p\n", id, A2R(shmc, item), slabs[id].free_item);
        }
    }

    if (!item) return item;

    item->clsid = id;
    item->next  = item->prev = item->h_next = 0;
    item->nkey  = nkey;
    item->nval  = nval;
    item->key   = (void *) A2R(shmc, &item->end[0]);
    item->val   = (void *) A2R(shmc, &item->end[0]) + nkey;
    return item;
}

static void item_free(shmc_t *shmc, shmc_item_t *item)
{
    /* find slab */
    int id = item_clsid(shmc, item->nkey, item->nval);
    shmc_slab_t *slabs = shmc->slabs;

    item->next= slabs[id].free_item;
    slabs[id].free_item = A2R(shmc, item);
    shmc_debug("slabs[%02d] add    %p, next %p\n", id, slabs[id].free_item, item->next);
}
