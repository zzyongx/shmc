#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <shmc.h>

/* export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:../"
 * gcc -o unit unit.c -Wall -g -I../ -L../ -lshmc
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

char *x(char c, size_t len) 
{
    return memset(malloc(len), c, len);
}

int main(int argc, char *argv[])
{
    const char *token = "/tmp/shmc.mmap";
    unlink(token);

    shmc_t *shmc;
    SHMC_RC rc;

    rc = shmc_init(token, 0, &shmc);
    test(rc == SHMC_ETOKEN, "shmc_init expect etoken ok",
            "shmc_init error", shmc_error(rc));

    /* init shmc attr */
    shmc_attr_t attr = SHMC_ATTR_INITIALIZER;
    shmc_attr_set_default_counter(&attr, 1);

    rc = shmc_init(token, &attr, &shmc);
    test(rc == SHMC_OK, "shmc_init create ok",
            "shmc_init create error", shmc_error(rc)); 

    {
        shmc_t *shmc;
        rc = shmc_init(token, &attr, &shmc);
        /* one shmc can't be create twice */
        test(rc == SHMC_ECREATE, "shmc_init expect ecreate ok",
                "shmc_init error", shmc_error(rc));
    }
    pid_t pid;
    if ((pid = fork()) == 0) {
        shmc_t *shmc;
        rc = shmc_init(token, &attr, &shmc);
        /* one shmc can't be create twice */
        test(rc == SHMC_ECREATE, "shmc_init expect ecreate ok",
                "shmc_init error", shmc_error(rc));
        exit(0);
    }

    if ((pid = fork()) == 0) {
        shmc_t *shmc;
        rc = shmc_init(token, 0, &shmc);
        /* we can attach after create */
        test(rc == SHMC_OK, "shmc_init attach ok",
                "shmc_init attach error", shmc_error(rc));
        shmc_destroy(shmc);
        exit(0);
    }

    while (wait(0) > 0);

    const char *key = "shmc";
    const size_t nkey = strlen(key);
    char *val;
    size_t nval;

    char *x16 = x('a', 16);
    char *x32 = x('b', 32);
    char *x64 = x('c', 64);
    char *x96 = x('d', 96);

    if ((pid = fork()) == 0) {
        shmc_t *shmc;
        rc = shmc_init(token, 0, &shmc);
        /* we can attach more than one time */
        test(rc == SHMC_OK, "shmc_init attach ok",
                "shmc_init attach error", shmc_error(rc));
        shmc_destroy(shmc);
        exit(0);
    }

    uint32_t flags = 1;
    uint64_t new_val;
    /* set default_counter, if key not exist, default 0 */
    rc = shmc_incr(shmc, key, nkey, 5, &new_val, &flags);
    test(rc == SHMC_OK && new_val == 5 && flags == 1,
            "5 after shmc_incr", "shmc_incr error", shmc_error(rc));

    rc = shmc_decr(shmc, key, nkey, 3, &new_val, &flags);
    test(rc == SHMC_OK && new_val == 2 && flags == 1,
            "2 after shmc_decr", "shmc_decr error", shmc_error(rc));

    /* if val < 0 after decr, set val = 0 */
    rc = shmc_decr(shmc, key, nkey, 7, &new_val, &flags);
    test(rc == SHMC_OK && new_val == 0 && flags == 1,
            "0 after shmc_decr", "shmc_decr error", shmc_error(rc));

    /* key is exist, add return SHMC_EXIST */
    rc = shmc_add(shmc, key, nkey, x32, 32, flags);
    test(rc == SHMC_EXIST, "shmc_add expect exist ok",
            "shmc_add error", shmc_error(rc));

    /* set return ok whether key is exist or not */
    rc = shmc_set(shmc, key, nkey, x32, 32, flags);
    test(rc == SHMC_OK, "shmc_set ok",
            "shmc_set error", shmc_error(rc));

    /* delete key */
    rc = shmc_del(shmc, key, nkey);
    test(rc == SHMC_OK, "shmc_del ok",
            "shmc_del error", shmc_error(rc));

    /* if key is not exist, replace return SHMC_NOTFOUND */
    rc = shmc_replace(shmc, key, nkey, x96, 96, flags);
    test(rc == SHMC_NOTFOUND, "shmc_replace expect notfound ok",
            "shmc_replace error", shmc_error(rc));

    /* if key is not exist, prepend return SHMC_NOTFOUND */
    rc = shmc_prepend(shmc, key, nkey, x64, 64, flags);
    test(rc == SHMC_NOTFOUND, "shmc_prepend expect notfound ok",
            "shmc_prepend error", shmc_error(rc));

    /* if key is not exist, append return SHMC_NOTFOUND */
    rc = shmc_append(shmc, key, nkey, x32, 32, flags);
    test(rc == SHMC_NOTFOUND, "shmc_append expect notfound ok",
            "shmc_append error", shmc_error(rc));

    flags = 32;

    /* key is not exist, add return SHMC_OK */
    rc = shmc_add(shmc, key, nkey, x32, 32, flags);
    test(rc == SHMC_OK, "shmc_add ok",
            "shmc_add error", shmc_error(rc));

    flags = 64;

    /* key is exist, replace return SHMC_OK */
    rc = shmc_replace(shmc, key, nkey, x64, 64, flags);
    test(rc == SHMC_OK, "shmc_replace ok",
            "shmc_replace error", shmc_error(rc));

    rc = shmc_get(shmc, key, nkey, &val, &nval, &flags);
    test(rc == SHMC_OK && nval == 64 &&
            memcmp(val, x64, nval) == 0 && flags == 64,
            "value after replace ok", "value after replace error", 0);

    /* key is exist, prepend return SHMC_OK */
    rc = shmc_prepend(shmc, key, nkey, x16, 16, flags);
    test(rc == SHMC_OK, "shmc_prepend ok",
            "shmc_prepend error", shmc_error(rc));

    rc = shmc_get(shmc, key, nkey, &val, &nval, &flags);
    test(rc == SHMC_OK && nval == (16 + 64) &&
         memcmp(val, x16, 16) == 0 &&
         memcmp(val + 16, x64, 64) == 0 && flags == 64,
         "value after prepend ok", "value after prepend error", shmc_error(rc));
    free(val);

    /* key is exist, append return SHMC_OK */
    rc = shmc_append(shmc, key, nkey, x96, 96, flags);
    test(rc == SHMC_OK && flags == 64, "shmc_append ok",
            "shmc_append error", shmc_error(rc));

    rc = shmc_get(shmc, key, nkey, &val, &nval, &flags);
    test(rc == SHMC_OK && nval == (16 + 64 + 96) &&
         memcmp(val, x16, 16) == 0 && memcmp(val + 16, x64, 64) == 0 &&
         memcmp(val + 16 + 64, x96, 96) == 0 && flags == 64, "value after append ok",
         "value after append error", shmc_error(rc));

    rc = shmc_replace(shmc, key, nkey, x16, 16, 0);
    test(rc == SHMC_OK, "shmc_replace ok",
            "shmc replace error", shmc_error(rc));

    char buffer[32];
    rc = shmc_getf(shmc, key, nkey, buffer, &nval, &flags);
    test(rc == SHMC_OK && nval == 16 && flags == 64,
         "shmc_getf ok", "shmc_getf error", shmc_error(rc));

    shmc_destroy(shmc);

    if ((pid = fork()) == 0) {
        shmc_t *shmc_reader;
        rc = shmc_init(token, 0, &shmc_reader);
        /* we can attach after create */
        test(rc == SHMC_OK, "shmc_init attach ok in related process",
                "shmc_init attach error in related process", shmc_error(rc));

        rc = shmc_get(shmc_reader, key, nkey, &val, &nval, 0);
        test(rc == SHMC_OK && nval == 16 && memcmp(val, x16, 16) == 0, "shmc_get ok",
                "shmc_get error", shmc_error(rc));

        shmc_destroy(shmc_reader);

        test(execv("./reader", 0) == -1, "startup reader",
                "can't startup reader", 0);
        exit(0);
    }
    while (wait(0) > 0);

    free(val);
    return 0;
}
