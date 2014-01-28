# Makefile for systems with GNU tools
CXX  = g++
CC   = gcc
INSTALL = install

CFLAGS  += -I/usr/local/include -I. -L/usr/local/lib 
LDFLAGS += -lshmc
PREDEF  += -D__STDC_FORMAT_MACROS

ifeq ($(DEBUG), 1)
    CFLAGS += -O0 -g3
    WARN   += -Wall -Wextra -Wno-comment -Wformat -Wimplicit -Wparentheses -Wswitch \
		     -Wunused
	PREDEF += -DLOG_ERROR
else
    CFLAGS += -O2 -g3
    WARN   += -Wall -Wextra -Wno-comment -Wformat -Wimplicit -Wparentheses -Wswitch -Wuninitialized \
		     -Wunused
endif

ifndef ($(INSTALLDIR))
	INSTALLDIR = /usr/local
endif


OBJS    = netshell.o mcshell.o token.o

netshell: $(OBJS)
	$(CXX) $(CFLAGS) $(CPPFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o:%.cc
	$(CXX) -o $@ $(WARN) $(CFLAGS) $(CPPFLAGS) $(PREDEF) -c $<

%.o:%.c
	$(CC) -o $@ $(WARN) $(CFLAGS) $(CPPFLAGS) $(PREDEF) -c $<

install:
	$(INSTALL) -D netshell $(DESTDIR)$(INSTALLDIR)/bin/netshell

clean:
	rm -f ./*.o
