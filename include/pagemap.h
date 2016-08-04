#ifndef PAGEMAP_H
#define PAGEMAP_H

/* Interface to Linux's /proc/<PID>/pagemap */

/* target == 0 means 'this process' */
unsigned long get_phys_addr(pid_t target, unsigned long virt_addr);

size_t get_phys_addrs(
	pid_t target,
	unsigned long virt_addr_start,
	unsigned long virt_addr_end,
	unsigned long *phys_addr,
	size_t num_addr);

#endif

