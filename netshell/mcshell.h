/* vim:expandtab:shiftwidth=4:tabstop=4:smarttab:
 */
#ifndef _MC_SEHLL_
#define _MC_SHELL_

#include <shmc/shmc.h>
#include <eventmgr.hpp>

struct stats_t {
	uint64_t get_cnts;
	uint64_t set_cnts;
	uint64_t del_cnts;
	uint64_t incr_cnts;
	uint64_t decr_cnts;
	uint64_t get_misses;
	uint64_t del_misses;
	uint64_t incr_misses;
	uint64_t decr_misses;
	uint64_t err_cnts;
};

class McShell {
public:
	McShell(shmc_t *shmc, int port, const char *inter);
	bool run();
	void stop();

private:
	shmc_t   *shmc_;
	EventMgr *em_;
	stats_t   stats_;
};

#endif
