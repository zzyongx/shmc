# Makefile for systems with GNU tools
CC  = gcc
INSTALL = install

CFLAGS  = -I. -fPIC
LDFLAGS = -lpthread
PREDEF  = -DENDIAN_LITTLE
LIBSHMC = libshmc.so

ifeq ($(SHMC_FAST), 1) 
	PREDEF += -DSHMC_FAST
	LIBSHMC = libshmc_fast.so
endif

ifeq ($(SHMC_VERBOSE), 1)
	PREDEF += -DSHMC_VERBOSE
endif

ifeq ($(DEBUG), 1)
    CFLAGS += -O0 -g3
    WARN   += -Wall -Wextra -Wno-comment -Wformat -Wimplicit -Wparentheses -Wswitch \
		     -Wunused
else
    CFLAGS += -O2 -g3
    WARN   += -Wall -Wextra -Wno-comment -Wformat -Wimplicit -Wparentheses -Wswitch -Wuninitialized \
		     -Wunused
    PREDEF += -DNDEBUG
endif

ifndef ($(INSTALLDIR))
	INSTALLDIR = /usr/local
endif

OBJS    = hash.o shmc.o

$(LIBSHMC): $(OBJS)
	$(CC) -shared $(CFLAGS) $(CFLAGS_SHELL) -o $@ $(OBJS) $(LDFLAGS) $(LDFLAGS_SHELL)

%.o:%.c
	$(CC) -o $@ $(WARN) $(CFLAGS) $(CFLAGS_SHELL) $(PREDEF) -c $<

install:
	install -D $(LIBSHMC) $(DESTDIR)$(INSTALLDIR)/lib/libshmc.so
	install -D shmc.h     $(DESTDIR)$(INSTALLDIR)/include/shmc/shmc.h

clean:
	rm -f ./*.o
