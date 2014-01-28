#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <shmc.h>

/* export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:../"
 * gcc -o reader reader.c -Wall -g -I../ -L../ -lshmc
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

int main(int argc, char *argv[])
{
    const char *token = "/tmp/shmc.mmap";

    const char *key = "shmc";
    const size_t nkey = strlen(key);
    char *val;
    size_t nval;

    char *x16 = memset(malloc(16), 'a', 16);

    SHMC_RC rc;
    shmc_t *shmc;

    rc = shmc_init(token, 0, &shmc);
    /* we can attach after create */
    test(rc == SHMC_OK, "shmc_init attach ok as unrelated process",
            "shmc_init attach error as unrelated process", shmc_error(rc));

    rc = shmc_get(shmc, key, nkey, &val, &nval, 0);
    test(rc == SHMC_OK && nval == 16 && memcmp(val, x16, 16) == 0, "shmc_get ok",
            "shmc_get error", shmc_error(rc));

    /* no interface */
    shmc_destroy(shmc);

    return 0;
}
