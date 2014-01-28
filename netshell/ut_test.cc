/* vim:expandtab:shiftwidth=4:tabstop=4:smarttab:
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libmemcached/memcached.h>

/* g++ -o ut_test ut_test.cc -Wall -g -lmemcached
 */

#define test(r, ok, error, extra) do {                                \
	if (r) {                                                          \
		if (ok) printf("[OK]    %03d %s\n", __LINE__, ok);             \
	} else {                                                          \
		if (extra) {                                                  \
			printf("[ERROR] %03d %s, %s\n", __LINE__, error, extra);  \
		} else {                                                      \
			printf("[ERROR] %03d %s\n", __LINE__, error);             \
		}                                                             \
		exit(EXIT_FAILURE);                                           \
	}                                                                 \
} while (0)

char *x(char c, size_t len)
{
	return (char *) memset(malloc(len), c, len);
}

int main(int argc, char *argv[])
{
	memcached_return rc;
	memcached_st *mc = memcached_create(0);
	memcached_server_add(mc, "127.0.0.1", 11217);

	printf("netshell unit test\n");

	const char *key = "netshell";
	const size_t nkey = strlen(key);
	char *val;
	size_t nval;
	time_t expire = 0;
	uint32_t flag = 0;

	char *x16 = x('a', 16);
	char *x32 = x('b', 32);
	char *x64 = x('c', 64);
	char *x96 = x('d', 96);

	/* delete key first */
	rc = memcached_delete(mc, key, nkey, 0);
	test(rc == MEMCACHED_SUCCESS, "memcached_delete ok",
			"memcached_delete error", memcached_strerror(mc, rc));

	uint64_t newVal;
	/* set default_counter, if key not exist, default 0 */
	rc = memcached_increment(mc, key, nkey, 5, &newVal);
	test(rc == MEMCACHED_SUCCESS && newVal == 5, "5 after memcached_increment",
			"memcached_increment error", memcached_strerror(mc, rc));

	rc = memcached_decrement(mc, key, nkey, 3, &newVal);
	test(rc == MEMCACHED_SUCCESS && newVal == 2, "2 after memcached_decrement",
			"memcached_decrement error", memcached_strerror(mc, rc));

	/* if val < 0 after decr, set val = 0 */
	rc = memcached_decrement(mc, key, nkey, 7, &newVal);
	test(rc == MEMCACHED_SUCCESS && newVal == 0, "0 after memcached_decrement",
			"memcached_decrement error", memcached_strerror(mc, rc));

	/* key is exist, add return MEMCACHED_DATA_EXISTS */
	rc = memcached_add(mc, key, nkey, x32, 32, expire, flag);
	test(rc == MEMCACHED_DATA_EXISTS, "memcached_add expect exist ok",
			"memcached_add error", memcached_strerror(mc, rc));

	/* set return MEMCACHED_SUCCESS no matter key is exist or not */
	rc = memcached_set(mc, key, nkey, x32, 32, expire, flag);
	test(rc == MEMCACHED_SUCCESS, "memcached_set ok",
			"memcached_set error", memcached_strerror(mc, rc));

	/* delete key */
	rc = memcached_delete(mc, key, nkey, 0);
	test(rc == MEMCACHED_SUCCESS, "memcached_delete ok",
			"memcached_delete error", memcached_strerror(mc, rc));

	/* if key is not exist, replace return MEMCACHED_NOTFOUND */
	rc = memcached_replace(mc, key, nkey, x96, 96, expire, flag);
	test(rc == MEMCACHED_NOTFOUND, "memcached_replace expect notfound ok",
			"memcached_replace error", memcached_strerror(mc, rc));

	/* if key is not exist, prepend return MEMCACHED_NOTFOUND */
	rc = memcached_prepend(mc, key, nkey, x64, 64, expire, flag);
	test(rc == MEMCACHED_NOTFOUND, "memcached_prepend expect notfound ok",
			"memcached_prepend error", memcached_strerror(mc, rc));

	/* if key is not exist, append return MEMCACHED_NOTFOUND */
	rc = memcached_append(mc, key, nkey, x32, 32, expire, flag);
	test(rc == MEMCACHED_NOTFOUND, "memcached_append expect notfound ok",
			"memcached_append error", memcached_strerror(mc, rc));

	/* if key is not exist, add return MEMCACHED_SUCCESS */
	rc = memcached_add(mc, key, nkey, x32, 32, expire, flag);
	test(rc == MEMCACHED_SUCCESS, "memcached_add ok",
			"memcached_add error", memcached_strerror(mc, rc));

	/* if key is exist, replace return MEMCACHED_SUCCESS */
	rc = memcached_replace(mc, key, nkey, x64, 64, expire, flag);
	test(rc == MEMCACHED_SUCCESS, "memcached_replace ok",
			"memcached_replace error", memcached_strerror(mc, rc));

	val = memcached_get(mc, key, nkey, &nval, &flag, &rc);
	test(rc == MEMCACHED_SUCCESS && val && nval == 64 &&
			memcmp(val, x64, nval) == 0, "memcached_get ok",
			"memcached_get error", memcached_strerror(mc, rc));
	free(val);

	/* key is exist, prepend return MEMCACHED_SUCCESS */
	rc = memcached_prepend(mc, key, nkey, x16, 16, expire, flag);
	test(rc == MEMCACHED_SUCCESS, "memcached_prepend ok",
			"memcached_prepend error", memcached_strerror(mc, rc));

	val = memcached_get(mc, key, nkey, &nval, &flag, &rc);
	test(rc == MEMCACHED_SUCCESS && val && nval == (16 + 64) &&
			memcmp(val, x16, 16) == 0 &&
			memcmp(val+16, x64, 64) == 0, "value after prepend ok",
			"value after prepend error", memcached_strerror(mc, rc));
	free(val);

	/* key is exist, append return MEMCACHED_SUCCESS */
	rc = memcached_append(mc, key, nkey, x96, 96, expire, flag);
	test(rc == MEMCACHED_SUCCESS, "memcached_append ok",
			"memcached_prepend error", memcached_strerror(mc, rc));

	val = memcached_get(mc, key, nkey, &nval, &flag, &rc);
	test(rc == MEMCACHED_SUCCESS && val && nval == (16 + 64 + 96) &&
			memcmp(val, x16, 16) == 0 && memcmp(val + 16, x64, 64) == 0 &&
			memcmp(val + 16 + 64, x96, 96) == 0, "value after append ok",
			"value after append error", memcached_strerror(mc, rc));
	free(val);

	/* replace */
	rc = memcached_replace(mc, key, nkey, x16, 16, expire, flag);
	test(rc == MEMCACHED_SUCCESS, "memcached_replace ok",
			"memcached_replace error", memcached_strerror(mc, rc));

	memcached_free(mc);

	return 0;
}
