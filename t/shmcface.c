#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <shmc.h>

/* gcc -o shmcface shmcface.c -Wall -g -lshmc
 */

int main(int argc, char *argv[])
{
    if ((argc != 4 && argc != 5) ||
        (strcmp(argv[2], "set") == 0 && argc != 5)) {
        printf("usage: shmcface token set|get|del key[ value]\n");
        return -1;
    }

    const char *token = argv[1];
    const char *key   = argv[3];

    SHMC_RC rc;
    shmc_t *shmc;

    if ((rc = shmc_init(token, 0, &shmc)) != SHMC_OK) {
        printf("shmc_init failed, %s\n", shmc_error(rc));
        return -1;
    }

    int code = -1;

    if (strcmp(argv[2], "set") == 0) {
        const char *val = argv[4];
        rc = shmc_set(shmc, key, strlen(key), val, strlen(val), 0);
        if (rc == SHMC_OK) {
            printf("STORED\n");
            code = 0;
        } else {
            printf("error, %s\n", shmc_error(rc));
        }
    } else if (strcmp(argv[2], "get") == 0) {
        char    *val;
        size_t   nval;
        uint32_t flags;
        rc = shmc_get(shmc, key, strlen(key), &val, &nval, &flags);
        if (rc == SHMC_OK) {
            printf("%u %.*s\n", flags, (int) nval, val);
            free(val);
            code = 0;
        } else {
            printf("error, %s\n", shmc_error(rc));
        }
    } else if (strcmp(argv[2], "del") == 0) {
        rc = shmc_del(shmc, key, strlen(key));
        if (rc == SHMC_OK || rc == SHMC_NOTFOUND) {
            printf("DELETED\n");
            code = 0;
        } else {
            printf("error, %s\n", shmc_error(rc));
        }
    } else {
        printf("usage: shmcface token set|get|del key[ value]\n");
    }

    shmc_destroy(shmc);

    return code;
}
