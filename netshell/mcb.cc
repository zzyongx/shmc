/* vim:expandtab:shiftwidth=4:tabstop=4:smarttab:
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vector>
#include <pthread.h>
#include <sys/time.h>

#include <libmemcached/memcached.h>
#include <shmc.h>

/* g++ -o mcb mcb.cc -Wall -g -lpthread -lshmc -lmemcached
 */

static void *mcReader(void *arg);
static void *mcWriter(void *arg);
static void *shmcReader(void *arg);
static void *shmcWriter(void *arg);

static char *host;
static int   port;
static char *token;

static const size_t MAX_KEY = 1000 * 1000;

static bool stop = false;
static void sigHandler(int signo)
{
	if (signo == SIGTERM || signo == SIGINT)
		stop = true;
}

int main(int argc, char *argv[])
{
	if (argc != 5 && argc != 4) {
		printf("usage: mcb nthread host port r|w|rw\n");
		printf("usage: mcb nthread token r|w|rw\n");
		return EXIT_SUCCESS;
	}

	char *mode;
	void *(*reader)(void *);
	void *(*writer)(void *);

	size_t nthread = atoi(argv[1]);
	if (argc == 5) {
		host = argv[2];
		port = atoi(argv[3]);
		reader = mcReader;
		writer = mcWriter;
		mode = argv[4];
	} else {
		token = argv[2];
		reader = shmcReader;
		writer = shmcWriter;
		mode = argv[3];
	}

	std::vector<pthread_t> tids;

	if (strchr(mode, 'r')) {
		for (size_t i = 0; i < nthread; ++i) {
			pthread_t tid;
			if (pthread_create(&tid, 0, reader, 0) != 0) {
				printf("pthread_create failed\n");
				return EXIT_FAILURE;
			}
			tids.push_back(tid);
		}
	}

	if (strchr(mode, 'w')) {
		for (size_t i = 0; i < nthread; ++i) {
			pthread_t tid;
			if (pthread_create(&tid, 0, writer, 0) != 0) {
				printf("pthread_create failed\n");
				return EXIT_FAILURE;
			}
			tids.push_back(tid);
		}
	}

	signal(SIGINT, sigHandler);
	signal(SIGTERM, sigHandler);

	for (size_t i = 0; i < tids.size(); ++i) {
		pthread_join(tids[i], 0);
	}

	return EXIT_FAILURE;
}

void *mcReader(void *arg)
{
	memcached_st *mc = memcached_create(0);
	memcached_server_add(mc, host, port);
	memcached_behavior_set(mc, MEMCACHED_BEHAVIOR_SND_TIMEOUT, 10 * 1000);
	memcached_behavior_set(mc, MEMCACHED_BEHAVIOR_RCV_TIMEOUT, 10 * 1000);

	memcached_return rc;
	char *val;
	size_t nval;
	uint32_t flag;
	char key[32];
	size_t nkey;

	while (!stop) {
		for (size_t i = 0; i < MAX_KEY && !stop; ++i) {
			nkey = sprintf(key, "%d", (int) i);
			val = memcached_get(mc, key, nkey, &nval, &flag, &rc);
			if (val) {
				free(val);
			} else if (rc != MEMCACHED_NOTFOUND) {
				printf("key: %s, %s\n", key, memcached_strerror(mc, rc));	
			}
		}
	}

	memcached_free(mc);
	return 0;
}

void *mcWriter(void *arg)
{
	memcached_st *mc = memcached_create(0);
	memcached_server_add(mc, host, port);
	memcached_behavior_set(mc, MEMCACHED_BEHAVIOR_SND_TIMEOUT, 10 * 1000);
	memcached_behavior_set(mc, MEMCACHED_BEHAVIOR_RCV_TIMEOUT, 10 * 1000);

	memcached_return rc;
	char *val = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";
	size_t nval = strlen(val);
	char key[32];
	size_t nkey;

	while (!stop) {
		for (size_t i = 0; i < MAX_KEY && !stop; i++) {
			nkey = sprintf(key, "%d", (int) i);
			rc = memcached_set(mc, key, nkey, val, nval, 0, 0);
			if (rc != MEMCACHED_SUCCESS) {
				printf("key: %s, %s\n", key, memcached_strerror(mc, rc));
			}
		}
	}

	memcached_free(mc);
	return 0;
}

inline int diff(struct timeval *begin, struct timeval *end)
{
	return ((end->tv_sec - begin->tv_sec) * 1000000 + (end->tv_usec - begin->tv_usec)) / 1000;
}

void *shmcReader(void *arg)
{
	shmc_t *shmc;
	SHMC_RC rc;

	rc = shmc_init(token, 0, &shmc);
	if (rc != SHMC_OK) {
		printf("shmc_init failed, %s\n", shmc_error(rc));
		return 0;
	}

	char *val;
	size_t nval;
	char key[32];
	size_t nkey;

	int d;
	struct timeval begin, end;

	while (!stop) {
		for (size_t i = 0; i < MAX_KEY && !stop; i++) {
			nkey = sprintf(key, "%d", (int) i);	
			gettimeofday(&begin, 0);
			rc = shmc_get(shmc, key, nkey, &val, &nval, 0);
			gettimeofday(&end, 0);
			if (rc == SHMC_OK) {
				free(val);
			} else if (rc != SHMC_NOTFOUND) {
				printf("key: %s, %s\n", key, shmc_error(rc));
			}

			if ((d = diff(&begin, &end)) > 1000) {
				printf("key: %s, timeout %d\n", key, d);
			}

		}	
	}

	shmc_destroy(shmc);
	return 0;
}

void *shmcWriter(void *arg)
{
	shmc_t *shmc;
	SHMC_RC rc;

	rc = shmc_init(token, 0, &shmc);
	if (rc != SHMC_OK) {
		printf("shmc_init failed, %s\n", shmc_error(rc));
		return 0;
	}

	char *val = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";
	size_t nval = strlen(val);
	char key[32];
	size_t nkey;

	int d;
	struct timeval begin, end;

	while (!stop) {
		for (size_t i = 0; i < MAX_KEY && !stop; i++) {
			nkey = sprintf(key, "%d", (int) i);	
			gettimeofday(&begin, 0);
			rc = shmc_set(shmc, key, nkey, val, nval, 0);
			gettimeofday(&end, 0);
			if (rc != SHMC_OK) {
				printf("key: %s, %s\n", key, shmc_error(rc));
			}

			if ((d = diff(&begin, &end)) > 1000) {
				printf("key: %s, timeout %d\n", key, d);	
			}
		}
	}

	shmc_destroy(shmc);
	return 0;
}
