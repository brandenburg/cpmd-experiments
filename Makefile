# ##############################################################################
# User variables

# user variables can be specified in the environment or in a .config file
-include .config

LIBLITMUS ?= ../liblitmus

# Include default configuration from liblitmus
include ${LIBLITMUS}/inc/config.makefile

# all sources
vpath %.c bin/

# ##############################################################################
# Targets

all = cache_cost memthrash

.PHONY: all clean
all: ${all}
clean:
	rm -f ${all} *.o *.d

obj-cache_cost = cache_cost.o
cache_cost: ${obj-cache_cost}

obj-memthrash  = memthrash.o
memthrash: ${obj-memthrash}

# dependency discovery
include ${LIBLITMUS}/inc/depend.makefile
