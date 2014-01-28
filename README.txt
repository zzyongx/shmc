shmc/netshell
=============
shmc is a key-value lib based on share memory. it's something like memcache 
except than it's base on *share memory* and it's a *lib*.

netshell is a net api to operate shmc use memcache protocol.

they are useful.

1. share something between process, shmc is very convenient than OS api.
2. you can query info in shmc through function API,
   but update it through net API(netshell).

for example:

A DNS server has lots of domain-ip keypairs, most time they are static,
when conf changes you can reload conf. when you provided DNS services,
the domain-ip keypair may CAUD anytime, and reload conf anytime will not work.

you can use db store the domain-ip keypairs, but is less efficiency 
because db is another process, even on another machine.

use shmc you can query it with api, no IPC, no network communicate.
use netshell update it through network.

read many write little but not rare, shmc/netshell is helpful.

install
=======
# make && make install

shmc api
========
@see shmc.h

In fact netshell is a program use shmc,
@see mcshell.cc netshell.cc

shmc_attr_t attr = SHMC_ATTR_INITIALIZER;

shmc_t *shmc;
shmc_init("/dev/shmc/x.db", &attr, &shmc);_

shmc_set(shmc, "key", 3, "value", 5, 0);

char *val;
size_t nval;
uint32_t flags;
shmc_get(shmc, "key", 3, &val, &nval, &flag);

char fval[32];
shmc_fget(shmc, "key", 3, fval, &nval, &flag);

netshell api
============
@see libmemcached
