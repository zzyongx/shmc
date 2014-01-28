/* vim:expandtab:shiftwidth=4:tabstop=4:smarttab:
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>

#include <shmc/shmc.h>
#include <mcshell.h>

McShell *mcShell = 0;

static void sigHandler(int signo)
{
	if (signo == SIGTERM || signo == SIGINT) {
		if (mcShell) mcShell->stop();
	}
}

static int usage(const char *error)
{
	if (error) fprintf(stderr, "%s\n\n", error);

	fprintf(stderr, "usage: netshell [option]\n"
			        "    -i interface to listen on (default: INADDR_ANY, all addresses)\n"
			        "    -p listen port, default 11217\n"
					"    -m max memory to use in megabytes (default: 64 MB)\n"
					"    -M return error on memory exhausted (rather than LRU)\n"
					"    -n <bytes>  minimum space allocated for key+value (default: 64)\n"
					"    -f <factor> chunk size growth factor (default: 2)\n"
					"    -P <file> save PID in <file>, only used with -d option\n"
					"    -I Override the size of each slab page. Adjusts max item size\n"
					"       (default: 1mb, min: 1k, max: 128m)\n"
					"    -d run as daemon, default no\n\n"
					"    -b max buckets number, set as max as enouth (default: 65536)\n"
					"    -t mmap file (default: /dev/shm/netshell.mmap)\n"
					"    -u token's mode (default: 0644)\n"
					"    -c use default counter, (default: no)\n"
					"    -l use flock, (default: pthread)\n"
					"    -a afresh new map, unlink old map, default: use old\n");
	return error ? EXIT_FAILURE : EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
	assert(SHMC_VERSION == shmc_version());

	const char *token = "/dev/shm/netshell.mmap";
    const char *inter = 0;
	int port = 11217;
	const char *pidfile = 0;
	int daemonize = 0;

	size_t memLimit = 64 * 1024 * 1024;
	int nbuckets = 65536;
	int mode = 0644;

	size_t minItem = 64;
	size_t maxItem = 1024 * 1024;
	float factor = 2;

	int evictToFree = 1;
	int defaultCounter = 0;
	int useFlock = 0;
    int useNewMap = 0;

	int c;
	while ((c = getopt(argc, argv, "i:p:m:Mn:f:P:I:db:t:u:clah")) > 0) {
		switch (c) {
            case 'i': inter = optarg; break;
			case 'p': port = atoi(optarg); break;
			case 'm': memLimit = atoi(optarg) * 1024 * 1024; break;
			case 'M': evictToFree = 0; break;
			case 'n': minItem = atoi(optarg); break;
			case 'f': factor = atof(optarg); break;
			case 'P': pidfile = optarg; break;
			case 'I': maxItem = atoi(optarg); break;
			case 'd': daemonize = 1; break;
			case 'b': nbuckets = atoi(optarg); break;
			case 't': token = optarg; break;
			case 'u': mode = atoi(optarg); break;
			case 'c': defaultCounter = 1; break;
			case 'l': useFlock = 1; break;
			case 'a': useNewMap = 1; break;
			case 'h': exit(usage(0)); break;
		}
	}

	if (maxItem < 1024 && maxItem > 128 * 1024 * 1024) {
		exit(usage("invalid -I parameter"));
	}

	shmc_attr_t attr = SHMC_ATTR_INITIALIZER;

	shmc_attr_set_mem_limit(&attr, memLimit);	
	shmc_attr_set_nbuckets(&attr, nbuckets);
	shmc_attr_set_mode(&attr, mode);
	shmc_attr_set_item_size_min(&attr, minItem);
	shmc_attr_set_item_size_max(&attr, maxItem);
	shmc_attr_set_item_size_factor(&attr, factor);
	shmc_attr_set_evict_to_free(&attr, evictToFree);
	shmc_attr_set_default_counter(&attr, defaultCounter);
	shmc_attr_use_flock(&attr, useFlock);

	if (daemonize) {
		daemon(1, 1);
	}

	if (pidfile) {
		FILE *fp = fopen(pidfile, "w");	
		if (fp) {
			fprintf(fp, "%d", (int) getpid());	
			fclose(fp);
		}
	}

	// unlink old mmap file
    if (useNewMap) {
	    unlink(token);
    }

	shmc_t *shmc;
	SHMC_RC rc = shmc_init(token, &attr, &shmc);
	if (rc != SHMC_OK) {
		fprintf(stderr, "can't init shmc %s\n", shmc_error(rc));
		exit(EXIT_FAILURE);
	}

	signal(SIGTERM, sigHandler);
	signal(SIGINT, sigHandler);
	signal(SIGPIPE, SIG_IGN);

	try {
		mcShell = new McShell(shmc, port, inter);
		mcShell->run();
	} catch (int eno) {
		fprintf(stderr, "can't startup netshell, %d:%s\n", eno, strerror(eno));
	}

	shmc_destroy(shmc);

	unlink(pidfile);
	return 0;
}
