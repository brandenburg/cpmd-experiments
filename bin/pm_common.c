/*
 * pm_common.c
 *
 * Read / write data samples on file in binary format
 * Perform first elaboration on the (possibily big) samples set
 */
#include "pm_common.h"

#define BLOCK_MUL 500
#define SBLOCK_SIZE 1024

/* the number of hot reads that we can find is the same
 * as the number of iterations we performed in pm_task
 */
#define NUMHOTREADS REFTOTAL
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

/*
 * Quick and dirty statistics to get a rough estimate of cache access times
 * It does not tell the difference between "good" and "bad" overall
 * sampling points, so max values coming out from this are not accurate
 */
#define WANT_STATISTICS
#ifdef WANT_STATISTICS
#include <math.h>
#define CFREQ 2128.207
#endif

#ifdef DEBUG
#define dprintf(arg...) fprintf(stderr,arg)
#else
#define dprintf(arg...)
#endif

/* simple sequential write on disk.
 * (concurrent writes must be protected)
 *
 * saved_data_entry is ~ 20 B; so 100000 Datapoinst are ~ 2MB
 */
int serialize_data_entry(char *filename, struct data_entry *samples, int num)
{
	int fd;
	int i, j;

	/* buffer some data in memory before writing */
	struct saved_data_entry to_save[SBLOCK_SIZE];

	fd = open(filename, O_WRONLY | O_APPEND | O_CREAT, 0660);
	if (fd == -1){
		perror("open");
		return -1;
	}

	for (i = 0; i < num / SBLOCK_SIZE; i++) {
		memset(to_save, 0, sizeof(struct saved_data_entry) * SBLOCK_SIZE);
		for (j = 0; j < SBLOCK_SIZE; j++) {
			to_save[j].access_type = samples[j].access_type;
			to_save[j].access_time =
				samples[j].access_time;
			to_save[j].cpu = samples[j].cpu;
			to_save[j].preemption_length =
				samples[j].preemption_length;
		}

		samples = &samples[j];

		if (write(fd, to_save, sizeof(struct saved_data_entry) * SBLOCK_SIZE) == -1) {
			close(fd);
			perror("Write failed\n");
			return -1;
		}
	}

	memset(to_save, 0, sizeof(struct saved_data_entry) * SBLOCK_SIZE);
	for (j = 0; j < num % SBLOCK_SIZE; j++) {
		to_save[j].access_type = samples[j].access_type;
		to_save[j].access_time =
			samples[j].access_time;
		to_save[j].cpu = samples[j].cpu;
		to_save[j].preemption_length =
			samples[j].preemption_length;
	}

	if (write(fd, to_save, sizeof(struct saved_data_entry) * j) == -1) {
		close(fd);
		perror("Write failed\n");
		return -1;
	}

	dprintf("Written %d entries\n", i*SBLOCK_SIZE + j);

	close(fd);
	return 0;
}

/*
 * Presumably, all data will be written on little endian machines.
 * I assume the binary format is little endian
 *
 * return -1 on error
 * return number of samples on success
 */
int read_sdata_entry(const char *filename, struct saved_data_entry **samples)
{
	int fd;
	int i,j;

	int num_samples, file_size;
	struct saved_data_entry block_read[BLOCK_MUL];

	int bytes_read;

	fd = open(filename, O_RDONLY);
	if(fd == -1){
		perror("open");
		return -1;
	}

	/* Compute file size */
	file_size = lseek(fd, 0, SEEK_END);
	if(file_size == -1){
		close(fd);
		perror("lseek");
		return -1;
	}

	/* Return to start position */
	if(lseek(fd, 0, SEEK_SET) == -1){
		close(fd);
		perror("lseek");
		return -1;
	}

	num_samples = file_size / sizeof(struct saved_data_entry);
	dprintf("N entries: %d\n", num_samples);

	/* Allocate memory for data_entry samples */
	*samples = (struct saved_data_entry *) (malloc(num_samples *
					sizeof(struct saved_data_entry)));
	if(*samples == NULL){
		close(fd);
		perror("malloc");
		return -1;
	}

	/* Read all the file */
	j = 0;
	do {
		/* Read file (in BLOCK_MUL * sizeof(saved_data_entrty) block size) */
		bytes_read = read(fd, &block_read, sizeof(struct saved_data_entry) * BLOCK_MUL);
		if (bytes_read == -1) {
			perror("Cannot read\n");
			close(fd);
			free(*samples);
			return -1;
		}

		for (i = 0; i < (bytes_read / sizeof(struct saved_data_entry)); i++, j++)
			(*samples)[j] = block_read[i];

	} while(bytes_read > 0);

	close(fd);

#ifdef VERBOSE_DEBUG
	for (i = 0; i < num_samples; i++)
		fprintf(stderr,"(%c) - ACC %llu, CPU %u, PLEN %llu\n",
				(*samples)[i].access_type,
				(*samples)[i].access_time, (*samples)[i].cpu,
				(*samples)[i].preemption_length);
#endif
	return num_samples;
}

#ifdef WANT_STATISTICS
/*
 * print min, max, avg, stddev for the vector
 * samples is the size of the population
 * cpufreq is in MHz
 */
void print_rough_stats(unsigned long long *vector, int samples, double cpufreq,
		int wss, int tss)
{
	unsigned long long min, max;
	long double mi, qi, num_diff;
	int i;

	 /* manage first value */
	 mi = vector[0];
	 qi = 0;

	 min = vector[0];
	 max = vector[0];

	 for (i = 1; i < (samples - 1); i++) {

		 if (vector[i] < min)
			 min = vector[i];
		 if (vector[i] > max)
			 max = vector[i];

		 num_diff = (long double)(vector[i] - mi);

		 mi += num_diff / ((long double)(i + 1));
		 qi += ((i) * (num_diff * num_diff)) / ((long double)(i + 1));
	 }

	 /* unbiased stddev should be computed on (samples - 2) */
	 /*
	fprintf(stderr, "CPUFREQ = %f\nValues in tick\n", cpufreq);
	fprintf(stderr, "max = %llu\nmin = %llu\nmean = %Lf\nstddev = %Lf\n",
		max, min, mi, sqrtl(qi / (samples - 2)));
	*/
	 fprintf(stderr, "# wss, tss, max, min, avg, stddev\n");
	 fprintf(stderr, "%d, %d, %.5f, %.5f,  %.5Lf,  %.5Lf\n",
		wss, tss,
		max / cpufreq, min / cpufreq, mi / cpufreq,
		sqrtl(qi / (samples - 2)) / cpufreq);
}
#endif

/*
 * get_valid_ovd(): get valid overheads from trace file
 *
 * input:
 * @filename:	input trace file name
 *
 * output:
 * @full_costs: array of all overheads and preemption length associated
 * 		with valid measures
 *
 * full_costs MUST be initialized before entering this function and MUST
 * be at least DATAPOINTS long
 *
 * @return:	number of valid measures read (implicit "true" length of
 *		output array.)
 *		If error return < 0
 */
int get_valid_ovd(const char *filename, struct full_ovd_plen **full_costs,
		int wss, int tss)
{
	struct saved_data_entry *samples;
	/* total number of samples */
	int num_samples;
	/* number of valid samples */
	int scount = 0;

	int i;

	/* do we have a valid hot read? */
	int valid_hot_reads = 0;
	/* how many consecutive hot reads? */
	int total_hot_reads = 0;
	/* do we have a valid hot cost? */
	int valid_hot_cost = 0;
	/* are the hot reads valid so far? */
	int no_invalid_reads = 1;
	/* what is the last cpu seen so far? */
	unsigned int l_cpu = 0;

	unsigned long long hot_cost;
#ifdef WANT_STATISTICS
	unsigned long long *valid_c_samples;
	unsigned long long *valid_h_samples;
	unsigned long long *valid_p_samples;
	int c_count;
	int h_count;
	int p_count;
#endif

	if ((num_samples = read_sdata_entry(filename, &samples)) < 0) {
		fprintf(stderr, "Cannot read %s\n", filename);
		return -1;
	}

	/* alloc an upper bound of the number of valid samples we can have */
	*full_costs = (struct full_ovd_plen*) malloc(num_samples *
					sizeof(struct full_ovd_plen));
	if (*full_costs == NULL) {
		fprintf(stderr, "Cannot allocate overhead array\n");
		free(samples);
		return -1;
	}
	memset(*full_costs, 0, num_samples * sizeof(struct full_ovd_plen));

#ifdef WANT_STATISTICS
	valid_c_samples = (unsigned long long *) malloc(num_samples *
			sizeof(unsigned long long));
	if (valid_c_samples == NULL) {
		fprintf(stderr, "Cannot allocate overhead array\n");
		free(samples);
		return -1;
	}
	valid_h_samples = (unsigned long long *) malloc(num_samples *
			sizeof(unsigned long long));
	if (valid_h_samples == NULL) {
		fprintf(stderr, "Cannot allocate overhead array\n");
		free(valid_c_samples);
		free(samples);
		return -1;
	}
	valid_p_samples = (unsigned long long *) malloc(num_samples *
			sizeof(unsigned long long));
	if (valid_p_samples == NULL) {
		fprintf(stderr, "Cannot allocate overhead array\n");
		free(valid_h_samples);
		free(valid_c_samples);
		free(samples);
		return -1;
	}
	memset(valid_c_samples, 0, num_samples * sizeof(unsigned long long));
	memset(valid_h_samples, 0, num_samples * sizeof(unsigned long long));
	memset(valid_p_samples, 0, num_samples * sizeof(unsigned long long));

	c_count = 0;
	h_count = 0;
	p_count = 0;
#endif
#ifdef VERBOSE_DEBUG
	fprintf(stderr, "Start collected overhead\n");
	/* write this on stderr so we can redirect it on a different stream */
	for (i = 0; i < num_samples; i++)
		fprintf(stderr, "(%c) - ACC %llu, CPU %u, PLEN %llu\n",
				samples[i].access_type,
				samples[i].access_time, samples[i].cpu,
				samples[i].preemption_length);
	fprintf(stderr, "End collected ovrhead\n");
#endif
	hot_cost = samples[0].access_time;

	/* get valid overheads reads */
	for (i = 0; i < num_samples; i++) {

		if (samples[i].access_type == 'H' ||
			samples[i].access_type == 'h') {
			/* NUMHOTREADS consecutive 'H' hot reads should
			 * (hopefully) appear. Take the minimum
			 * of all valid reads up to when the first
			 * invalid 'h' read appears.
			 */
			total_hot_reads++;
			if (no_invalid_reads && samples[i].access_type == 'H') {

				valid_hot_reads++;
				if(valid_hot_reads == 1) {
					hot_cost = samples[i].access_time;
				}
				else {
					hot_cost = min(hot_cost, samples[i].access_time);
				}

			} else {
				/* no valid hot reads found */
				no_invalid_reads = 0;
			}

			if (total_hot_reads == NUMHOTREADS) {
				/* check if we have a valid hotread value */
				if (valid_hot_reads > 0)
					valid_hot_cost = 1;
				else
					valid_hot_cost = 0;

				/* reset flags */
				valid_hot_reads = 0;
				total_hot_reads = 0;
				no_invalid_reads = 1;
			}

			/* update last seen cpu */
			l_cpu = samples[i].cpu;

		} else {
			if (samples[i].access_type == 'P' ||
				samples[i].access_type == 'p') {

				/* this may be a preemption or a migration
				 * but we do not care now: just report it
				 * if it happened after a valid hot read
				 * and the preemption measure is valid
				 */
				if (valid_hot_cost && samples[i].access_type == 'P') {

					(*full_costs)[scount].curr_cpu = samples[i].cpu;
					(*full_costs)[scount].last_cpu = l_cpu;
					(*full_costs)[scount].ovd = (long long)
						samples[i].access_time - hot_cost;

					(*full_costs)[scount].plen = (long long)
						samples[i].preemption_length;

					dprintf("%u %u %lld %lld\n", (*full_costs)[scount].curr_cpu,
							(*full_costs)[scount].last_cpu,
							(*full_costs)[scount].ovd, (*full_costs)[scount].plen);

					scount++;
				}

				/* update last seen cpu */
				l_cpu = samples[i].cpu;
			}
		}
#ifdef WANT_STATISTICS
		if (samples[i].access_type == 'C')
			valid_c_samples[c_count++] = samples[i].access_time;
		else if (samples[i].access_type == 'H')
			valid_h_samples[h_count++] = samples[i].access_time;
		else if (samples[i].access_type == 'P')
			valid_p_samples[p_count++] = samples[i].access_time;
#endif
	}

	dprintf("End of valid entries\n");
#ifdef WANT_STATISTICS
	fprintf(stderr, "# Cold cache\n");
	print_rough_stats(valid_c_samples, c_count, CFREQ, wss, tss);
	fprintf(stderr, "# Hot cache\n");
	print_rough_stats(valid_h_samples, h_count, CFREQ, wss, tss);
	fprintf(stderr, "# After preemption\n");
	print_rough_stats(valid_p_samples, p_count, CFREQ, wss, tss);
	fprintf(stderr, "## Nsamples(c,h,p): %d, %d, %d\n",
			c_count, h_count, p_count);

	free(valid_p_samples);
	free(valid_h_samples);
	free(valid_c_samples);
#endif

	free(samples);
	return scount;
}

/*
 * TODO we are not using this function anymore as the description of the
 * 	cpus topology for our systems (xeon) doesn't match the cpu
 * 	number assignment implied by this function to work correctly.
 * 	Should be fixed at some point (also because i7 uses a different
 * 	cpu assignment). --- See below for the currently used function.
 *
 * get_ovd_plen(): 	get overheads and preemption/migration length for
 * 			different cores configurations
 *
 * For most architecture we can have at most 3 cache levels on the same chip
 * and then off chip migrations. In the worst case we need to measure:
 * [1] same core preemption, [2] same L2 migration,
 * [3] same L3 (different L2, same chip) migration, [4] off chip migration.
 *
 * Linux is processing _physical_ CPUs in a "linear" fashion, assigning a
 * sequence number to one core on a physical cpu and then jumping
 * on the next physical cpu. Look in sysfs for more details on cpu
 * topology. This doesn't seems to apply to NUMA machines (e.g., Opteron 8212,
 * Pound -> Nehalem i7) so the following function is probably working there
 * but we need to check the topology first...
 *
 * input:
 * @full_costs:		see get_valid_ovd()
 * @num_samples:	number of meaningful samples in full_costs
 *			(and in output arrays)
 * @cores_per_l2:	how many cores share an l2 cache (read below)
 * @cores_per_chip:	guess :)
 *
 * output:
 * @preempt:		[1]
 * @samel2:		[2]
 * @samechip:		[3]
 * @offchip:		[4]
 *
 * if samel2 is NULL, then L3 is not present and samel2 is equivalent to
 * samechip. cores_per_l2 should be equal to cores_per_chip, but is not used.
 */
void get_ovd_plen(struct full_ovd_plen *full_costs, int num_samples,
		unsigned int cores_per_l2, unsigned int cores_per_chip,
		struct ovd_plen *preempt, int *pcount,
		struct ovd_plen *samel2, int *l2count,
		struct ovd_plen *samechip, int *chipcount,
		struct ovd_plen *offchip, int *offcount)
{
	int i;
	*pcount = 0;
	*l2count = 0;
	*chipcount = 0;
	*offcount = 0;

	unsigned int curr_cpu;
	unsigned int last_cpu;

	for (i = 0; i < num_samples; i++) {
		dprintf("i = %d\n", i);
		curr_cpu = full_costs[i].curr_cpu;
		last_cpu = full_costs[i].last_cpu;

		if (curr_cpu == last_cpu) {
			dprintf("preempt\n");
			/* preemption */
			preempt[*pcount].ovd = full_costs[i].ovd;
			preempt[*pcount].plen = full_costs[i].plen;
			(*pcount)++;

			continue;

		}

		if (samel2) {
			dprintf("l2\n");

			if ((curr_cpu / cores_per_l2) == (last_cpu / cores_per_l2)) {
				dprintf("same L2\n");
				/* same L2 migration */
				samel2[*l2count].ovd = full_costs[i].ovd;
				samel2[*l2count].plen = full_costs[i].plen;
				(*l2count)++;

				continue;
			}

			if (((curr_cpu / cores_per_l2) != (last_cpu / cores_per_l2)) &&
					((curr_cpu / cores_per_chip) == (last_cpu / cores_per_chip))) {
				dprintf("same L3\n");
				/* same L3 migration */
				samechip[*chipcount].ovd = full_costs[i].ovd;
				samechip[*chipcount].plen = full_costs[i].plen;
				(*chipcount)++;

				continue;
			}
		} else {
			dprintf("same chip\n");
			/* samel2 == NULL */
			/* check same chip migration */
			if ((curr_cpu / cores_per_chip) == (last_cpu / cores_per_chip)) {

				samechip[*chipcount].ovd = full_costs[i].ovd;
				samechip[*chipcount].plen = full_costs[i].plen;
				(*chipcount)++;

				continue;
			}
		}
		dprintf("offchip\n");
		/* if we are here it should have been a offchip migration */
		offchip[*offcount].ovd = full_costs[i].ovd;
		offchip[*offcount].plen = full_costs[i].plen;
		(*offcount)++;
	}
	dprintf("pcount = %d\n", *pcount);
	dprintf("chipcount = %d\n", *chipcount);
	dprintf("l2count = %d\n", *l2count);
	dprintf("offcount = %d\n", *offcount);
}

/*
 * get_ovd_plen_umaxeon():	get overheads and preemption/migration length
 * 				for different cores conf. on uma xeon
 *
 * See above comments. This should probably work on most xeon (at least on
 * jupiter and ludwig)
 *
 * input:
 * @full_costs:		see get_valid_ovd()
 * @num_samples:	number of meaningful samples in full_costs
 *			(and in output arrays)
 * @cores_per_l2:	how many cores share an l2 cache (read below)
 * @num_phys_cpu:	guess :)
 *
 * output:
 * @preempt:		[1]
 * @samel2:		[2]
 * @samechip:		[3]
 * @offchip:		[4]
 *
 * FIXME: samel2 == NULL to say that L3 is not there... is tricky...
 * if samel2 is NULL, then L3 is not present and samel2 is equivalent to
 * samechip. cores_per_l2 should be equal to cores_per_chip, but is not used.
 */
void get_ovd_plen_umaxeon(struct full_ovd_plen *full_costs, int num_samples,
		unsigned int cores_per_l2, unsigned int num_phys_cpu,
		struct ovd_plen *preempt, int *pcount,
		struct ovd_plen *samel2, int *l2count,
		struct ovd_plen *samechip, int *chipcount,
		struct ovd_plen *offchip, int *offcount)
{
	int i;
	*pcount = 0;
	*l2count = 0;
	*chipcount = 0;
	*offcount = 0;

	unsigned int curr_cpu;
	unsigned int last_cpu;

	for (i = 0; i < num_samples; i++) {

		dprintf("i = %d\n", i);
		curr_cpu = full_costs[i].curr_cpu;
		last_cpu = full_costs[i].last_cpu;

		if (curr_cpu == last_cpu) {
			dprintf("preempt\n");
			/* preemption */
			preempt[*pcount].ovd = full_costs[i].ovd;
			preempt[*pcount].plen = full_costs[i].plen;
			(*pcount)++;

			continue;
		}

		if ((curr_cpu % num_phys_cpu) == (last_cpu % num_phys_cpu)) {
			/* ok, both cpus on the same chip, which caches do they shares? */
			if (samel2) {
				/* we have both L3 and L2.
				 * We already know we are sharing L3 */
				if (((curr_cpu / num_phys_cpu) / cores_per_l2) ==
					((last_cpu / num_phys_cpu) / cores_per_l2)) {
					/* they share also L2 */
					dprintf("same L2\n");
					samel2[*l2count].ovd = full_costs[i].ovd;
					samel2[*l2count].plen = full_costs[i].plen;
					(*l2count)++;

					continue;
				} else {
					/* this is an L3 migration */
					dprintf("same L3\n");
					samechip[*chipcount].ovd = full_costs[i].ovd;
					samechip[*chipcount].plen = full_costs[i].plen;
					(*chipcount)++;

					continue;
				}
			} else {
				/* ok, just L2 on this machine, this is an L2 migration */
				samechip[*chipcount].ovd = full_costs[i].ovd;
				samechip[*chipcount].plen = full_costs[i].plen;
				(*chipcount)++;

				continue;
			}
		}

		dprintf("offchip\n");
		/* if we are here it should have been an offchip migration */
		offchip[*offcount].ovd = full_costs[i].ovd;
		offchip[*offcount].plen = full_costs[i].plen;
		(*offcount)++;
	}
	dprintf("pcount = %d\n", *pcount);
	dprintf("chipcount = %d\n", *chipcount);
	dprintf("l2count = %d\n", *l2count);
	dprintf("offcount = %d\n", *offcount);
}

