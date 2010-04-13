/*
 * preemption and migration overhead measurement
 *
 * common data structures and defines
 */
#ifndef PM_COMMON_H
#define PM_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* WSS, CACHESIZE, DATAPOINTS may be given as commandline define
 * when ricompiling this test for different WSS, CACHESIZE and (?) datapoints
 * ATM only WSS can be passed through scons building mechanism
 */

/* Definitions and variables related to experimental measurement.
 * What I eventually want is a test script that will cycle though
 * different WSS and CACHESIZE, recompiling this program at
 * each round (easier for memory management), but running all test
 * without human intervention
 */
/*
 * default working set size, in KB
 * non-default WSS are taken from the test script (-DWSS=...)
 */
#ifndef WSS
#define WSS	3072
#endif
/* Cache size:
 * Niagara: L2: 3MB
 * Koruna: L2: 6MB every 2 cores
 * Ludwig: L2: 3MB every 2 cores, L3 12MB
 * Pound:  L2: 256KB, L3 8MB
 */
#define CACHESIZE	(12 * 1024)

/* number of measurements that can be stored per single pm_task */
#define DATAPOINTS	100000

/* The following macro don't need (hopefully) any modification */

/* Cache alignment (cache line size)
 * Niagara, Koruna, Ludwig, Pound cache line size: 64B
 */
#define CACHEALIGNMENT	64
/* ints per WSS */
#define	INTS_PER_WSS	(WSS*1024)/(sizeof(int))
/* reads vs. writes ratio */
#define READRATIO	75
/* random seed */
#define SEEDVAL		12345
/* number of "working sets" to cycle through */
#define NUMWS		((CACHESIZE*2)/WSS)+2
/* runtime in milliseconds -- 60s*/
#define SIMRUNTIME	60000
/* times to read warm memory to get accurate data */
/* preliminary experiments on Ludwig shows that we can safely set
 * this to just 2 iteration (first and second 'H' access are ~ equal)
 * (it was 3)
 */
#define REFTOTAL	2

#define NS_PER_MS	1000000

struct data_entry {
	unsigned long long timestamp;

	/* cC cold cache access
	 * hH hot cache access
	 * pP preeption / migration
	 */
	char access_type;
	unsigned long long access_time;

	unsigned int cpu;
	unsigned long job_count;
	unsigned long sched_count;
	unsigned long last_rt_task;
	unsigned long long preemption_length;
};

/* serializable data entry */
struct saved_data_entry {
	char access_type;
	unsigned long long access_time;
	unsigned int cpu;
	unsigned long long preemption_length;
};

/* long long is a looot of time and should be enough for our needs
 * However we keep the saved data in ull and leave to the analysis
 * dealing with the conversion
 */
struct full_ovd_plen {
	/* "current" cpu */
	unsigned int curr_cpu;
	/* last "seen" cpu (curr != last --> migration) */
	unsigned int last_cpu;
	/* overhead */
	long long ovd;
	/* preemption length */
	long long plen;
};

struct ovd_plen {
	long long ovd;
	long long plen;
};

/* write data_entry -> saved_data_entry on disk */
int serialize_data_entry(char *filename, struct data_entry *samples, int num);
/* read saved_data_entry from disk */
int read_sdata_entry(const char *filename, struct saved_data_entry **samples);

/* get valid overhead from trace file */
int get_valid_ovd(const char *filename, struct full_ovd_plen **full_costs,
		int wss, int tss);

/* get ovd and pm length for different cores configurations (on uma xeon) */
/* Watch out for different topologies:
 * /sys/devices/system/cpu/cpuX/cache/indexY/shared_cpu_list
 */
void get_ovd_plen_umaxeon(struct full_ovd_plen *full_costs, int num_samples,
		unsigned int cores_per_l2, unsigned int num_phys_cpu,
		struct ovd_plen *preempt, int *pcount,
		struct ovd_plen *samel2, int *l2count,
		struct ovd_plen *samechip, int *chipcount,
		struct ovd_plen *offchip, int *offcount);

/* get ovd and pm length for different cores configurations */
void get_ovd_plen(struct full_ovd_plen *full_costs, int num_samples,
		 unsigned int cores_per_l2, unsigned int cores_per_chip,
		 struct ovd_plen *preempt, int *pcount,
		 struct ovd_plen *samel2, int *l2count,
		 struct ovd_plen *samechip, int *chipcount,
		 struct ovd_plen *offchip, int *offcount);
#endif
