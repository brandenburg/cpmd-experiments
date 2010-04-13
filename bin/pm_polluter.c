/*
 * pm_be_polluter.c
 *
 * Best Effort Cache Polluter Task
 */
#include "pm_common.h"
#include "pm_arch.h"
#include "asm.h"

int mem_block[NUMWS][INTS_PER_WSS] __attribute__ ((aligned(CACHEALIGNMENT)));

int main(int argc, char **argv)
{
	int read, *loc_ptr;

	memset(&mem_block, 0, sizeof(int) * NUMWS * INTS_PER_WSS);

	/* Initialize random library for read/write ratio enforcement. */
	srandom(SEEDVAL);

	while(1) {
		read = (random() % 100) < READRATIO;
		loc_ptr = &mem_block[random() % NUMWS][0];
		loc_ptr += (random() % INTS_PER_WSS);

		barrier();

		if (read)
			read_mem(loc_ptr);
		else
			write_mem(loc_ptr);

		/* FIXME is really needed? */
		usleep(40);
	}

	/* never reached */
	return 0;
}

