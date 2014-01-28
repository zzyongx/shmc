#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <inttypes.h>
#include <unistd.h>
#include <shmc.h>

/* export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:../"
 * gcc -o load load.c -Wall -g -I../ -L../ -lshmc_fast
 */

void test(int b, const char *ok, const char *error, const char *extra)
{
    if (b) {
        if (ok) printf("[OK]    %s\n", ok);
    } else {
        if (extra) {
            printf("[ERROR] %s:%s\n", error, extra);
        } else {
            printf("[ERROR] %s\n", error);
        }
        exit(1);
    }
}

typedef struct {
    void *dumb;
    size_t size;
    size_t count;
} slab_t;

typedef struct {
    char *val;
    size_t nval;
} val_t;

static int stop = 0;
static void sighandle(int signo) { stop = 1; }

#define C 0

int main()
{
    const char *token = "/tmp/shmc.mmap";
    unlink(token);

    SHMC_RC rc;
    shmc_t *shmc;
    shmc_attr_t attr = SHMC_ATTR_INITIALIZER;
    shmc_attr_set_mem_limit(&attr, 512 * 1024 * 1024);
    shmc_attr_set_nbuckets(&attr, 100 * 10000);
    shmc_attr_set_evict_to_free(&attr, 0);

    signal(SIGINT, sighandle);
    signal(SIGTERM, sighandle);

    rc = shmc_init(token, &attr, &shmc);
    test(rc == SHMC_OK, "shmc_init ok",
            "shmc_init error", shmc_error(rc));

    /* build test data */
    size_t nitem = 0;

    /* shmc_slab_t is private for user
     * we define it for test
     */
    slab_t *slabs = (slab_t *)shmc->slabs;
    int i, j, k;
    for (i = 0; i < shmc->attr->slabs_count; ++i) {
        nitem += slabs[i].count;
    }

    val_t *vals = malloc(sizeof(val_t) * nitem);
    for (i = 0, j = 0; i < shmc->attr->slabs_count; ++i) {
        int len = slabs[i].size - 36 - 10; /* decr sizeof item and key */
        char *raw = memset(malloc(len), 'a'+i, len); 
        for (k = 0; k < slabs[i].count; ++k, ++j) {
            vals[j].val = raw;
            vals[j].nval = len;
        }
    }

    // random_shuffle
    srand(time(0));
    for (i = 0; i < nitem; ++i) {
        j = rand() % nitem; 
        val_t tmp = vals[i];
        vals[i] = vals[j];
        vals[j] = tmp;
    }

    uint64_t cnt = 0;
    char key[32];
    size_t nkey;

    const size_t NVAL = 1.5 * 1024 * 1024;
    size_t nval;
    char *val = malloc(NVAL);

    // writer
    j = 0;
    for (i = 0; i < 100 * 10000 && !stop; ++i) {
        if (++cnt == 0) printf("writer counter rewind\n");
        nkey = sprintf(key, "%d", i);
        rc = shmc_set(shmc, key, nkey, vals[j].val, vals[j].nval, 0);
        if (rc != SHMC_OK) printf("shmc_set error %s\n", shmc_error(rc));
        if (++j == nitem) j = 0;
    }

    printf("writer counter %"PRIu64"\n", cnt);

    pid_t pid[C];

    /* read process */
    for (k = 0; k < C; ++k) {
        if ((pid[k] = fork()) == 0) {
            test(execv("./load_reader", 0) == -1, "startup reader ok",
                    "can't startup reader", 0);

            exit(0);
        }
    }


    uint64_t hits = 0, miss = 0;
    while (!stop) {
        for (i = 0; i < 100 * 10000 && !stop; ++i) {
            nkey = sprintf(key, "%d", i);
            nval = NVAL;
            rc = shmc_getf(shmc, key, nkey, val, &nval, 0);
            if (rc == SHMC_OK) {
                if (++hits == 0) printf("reader counter hits rewind\n");
            } else if (rc == SHMC_NOTFOUND) {
                if (++miss == 0) printf("reader counter miss rewind\n");
            } else {
                printf("shmc_get error %s\n", shmc_error(rc)); 
            }
        } 
    }

    printf("reader counter hits %"PRIu64"\n", hits);
    printf("reader counter miss %"PRIu64"\n", miss);

    for (i = 0; i < C; ++i) {
        if (pid[i] > 0) kill(pid[i], SIGINT);
    }

    while (wait(0) > 0);

    return 0;
}
