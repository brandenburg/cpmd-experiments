# ##############################################################################
# User variables

# user variables can be specified in the environment or in a .config file
-include .config

# all sources
vpath %.c bin/

# ##############################################################################
# Flags

CPPFLAGS = -I./include
CFLAGS = -Wall -O0 -g

# ##############################################################################
# Targets

.PHONY: all clean

all = cache_cost memthrash

all: ${all}
clean:
	rm -f ${all} *.o *.d

obj-cache_cost = cache_cost.o pagemap.o
cache_cost: ${obj-cache_cost}

# 
# obj-memthrash  = memthrash.o
# memthrash: ${obj-memthrash}

