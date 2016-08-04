#define _GNU_SOURCE /* for sched_setaffinity */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sched.h>

#include <sys/io.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>

#if defined(__i386__) || defined(__x86_64__)
#include "x86-cycles.h"
#include "x86-irq.h"
#else
#error unsupported architecture
#endif

#include "pagemap.h"

static void die(char *error)
{
	fprintf(stderr, "Error: %s (errno: %m)\n",
		error);
	exit(1);
}

static int num_online_cpus()
{
	return sysconf(_SC_NPROCESSORS_ONLN);
}

int linux_migrate_to(int target_cpu)
{
	cpu_set_t *cpu_set;
	size_t sz;
	int num_cpus;
	int ret;

	if (target_cpu < 0)
		return -1;

	num_cpus = num_online_cpus();
	if (num_cpus == -1)
		return -1;

	if (target_cpu >= num_cpus)
		return -1;

	cpu_set = CPU_ALLOC(num_cpus);
	sz = CPU_ALLOC_SIZE(num_cpus);
	CPU_ZERO_S(sz, cpu_set);
	CPU_SET_S(target_cpu, sz, cpu_set);

	/* apply to caller */
	ret = sched_setaffinity(getpid(), sz, cpu_set);

	CPU_FREE(cpu_set);

	return ret;
}


static int check_migrations(int num_cpus)
{
	int cpu, err;

	for (cpu = 0; cpu < num_cpus; cpu++) {
		err = linux_migrate_to(cpu);
		if (err != 0) {
			fprintf(stderr, "Migration to CPU %d failed: %m.\n",
				cpu + 1);
			return 1;
		}
	}
	return 0;
}

static int become_posix_realtime_task(void)
{
	struct sched_param param;

	param.sched_priority = sched_get_priority_max(SCHED_FIFO);
	return sched_setscheduler(0 /* self */, SCHED_FIFO, &param);
}

/* must be larger than the largest cache in the system */
#define ARENA_SIZE_MB 1024
#define INTS_IN_1KB (1024 / sizeof(int))
#define ARENA_SIZE (INTS_IN_1KB * 1024 * ARENA_SIZE_MB)
static int page_idx = 0;
static int arena[ARENA_SIZE];

static int lock_memory(void)
{
	page_idx = getpagesize() / sizeof(int);
	return mlockall(MCL_CURRENT | MCL_FUTURE);
}

static void touch_arena(void) {
	int i;
	for (i = 0; i < ARENA_SIZE; i++)
		arena[i] = i;
}

static int arena_pos = 0;

static void reset_arena(void) {
	arena_pos = 0;
	touch_arena();
}

static int* allocate(int wss)
{
	int size = wss * INTS_IN_1KB;
	int *mem;

	/* Don't allow re-use between allocations.
	 * At most half of the arena may be used
	 * at any one time.
	 */
	if (size * 2 > ARENA_SIZE)
		die("static memory arena too small");

	if (arena_pos + size > ARENA_SIZE) {
		/* wrap to beginning */
		mem = arena;
		arena_pos = size;
	} else {
		mem = arena + arena_pos;
		arena_pos += size;
	}

	return mem;
}

static void deallocate(int *mem)
{
}

static void migrate_to(int target)
{
	if (linux_migrate_to(target) != 0)
		die("migration failed");
}

static void sleep_us(int microseconds)
{
	struct timespec delay;

	delay.tv_sec = 0;
	delay.tv_nsec = microseconds * 1000;
	if (nanosleep(&delay, NULL) != 0)
		die("sleep failed");
}

static int touch_mem(int *mem, int wss, int write_cycle)
{
	int sum = 0, i;

	if (write_cycle > 0) {
		for (i = 0; i < wss * 1024 / sizeof(int); i++) {
			if (i % write_cycle == (write_cycle - 1))
				mem[i]++;
			else
				sum += mem[i];
		}
	} else {
		/* sequential access, pure read */
		for (i = 0; i < wss * 1024 / sizeof(int); i++)
			sum += mem[i];
	}
	return sum;
}

static int pick_cpu(int last_cpu, int num_cpus)
{
	int cpu;
	if (num_cpus == 1 || random() % 2 == 0)
		return last_cpu; /* preemption */
	else {
		do {
			cpu = random() % num_cpus;
		} while (cpu == last_cpu);
		return cpu;
	}
}

static void do_random_experiment(FILE* outfile,
				 int num_cpus, int wss,
				 int sleep_min, int sleep_max,
				 int write_cycle, int sample_count,
				 int best_effort)
{
	int last_cpu, next_cpu, delay, show = 1, i;
	unsigned long preempt_counter = 0;
	unsigned long migration_counter = 0;
	unsigned long counter = 1;
	unsigned long num_pages = wss / getpagesize();
	unsigned long *phys_addrs;

	cycles_t start, stop;
	cycles_t cold, hot1, hot2, hot3, after_resume;

	int *mem;

	if (!num_pages)
		num_pages = 1;

	phys_addrs = malloc(sizeof(long) * num_pages);

	migrate_to(0);
	last_cpu = 0;

	/* prefault and dirty cache */
	reset_arena();

#if defined(__i386__) || defined(__x86_64__)
	if (!best_effort)
		iopl(3);
#endif

	fprintf(outfile,
		"# %5s, %6s, %6s, %6s, %3s, %3s"
		", %10s, %10s, %10s, %10s, %10s"
		", %12s, %12s"
		"\n",
		"COUNT", "WCYCLE",
		"WSS", "DELAY", "SRC", "TGT", "COLD",
		"HOT1", "HOT2", "HOT3", "WITH-CPMD",
		"VIRT ADDR", "PHYS ADDR");


	while (!sample_count ||
	       sample_count >= preempt_counter ||
	       (num_cpus > 1 && sample_count >= migration_counter)) {

		delay = sleep_min + random() % (sleep_max - sleep_min + 1);
		next_cpu = pick_cpu(last_cpu, num_cpus);

		if (sample_count)
			show = (next_cpu == last_cpu && sample_count >= preempt_counter) ||
				(next_cpu != last_cpu && sample_count >= migration_counter);

		mem = allocate(wss);

#if defined(__i386__) || defined(__x86_64__)
		if (!best_effort)
			cli();
#endif
		start = get_cycles();
		mem[0] = touch_mem(mem, wss, write_cycle);
		stop  = get_cycles();
		cold = stop - start;

		start = get_cycles();
		mem[0] = touch_mem(mem, wss, write_cycle);
		stop  = get_cycles();
		hot1 = stop - start;

		start = get_cycles();
		mem[0] = touch_mem(mem, wss, write_cycle);
		stop  = get_cycles();
		hot2 = stop - start;

		start = get_cycles();
		mem[0] = touch_mem(mem, wss, write_cycle);
		stop  = get_cycles();
		hot3 = stop - start;
#if defined(__i386__) || defined(__x86_64__)
		if (!best_effort)
			sti();
#endif
		migrate_to(next_cpu);
		sleep_us(delay);

#if defined(__i386__) || defined(__x86_64__)
		if (!best_effort)
			cli();
#endif
		start = get_cycles();
		mem[0] = touch_mem(mem, wss, write_cycle);
		stop  = get_cycles();
#if defined(__i386__) || defined(__x86_64__)
		if (!best_effort)
			sti();
#endif
		after_resume = stop - start;


		/* run, write ratio, wss, delay, from, to, cold, hot1, hot2,
		 * hot3, after_resume */
		if (show) {
			fprintf(outfile,
				" %6ld, %6d, %6d, %6d, %3d, %3d, "
				"%10" CYCLES_FMT ", "
				"%10" CYCLES_FMT ", "
				"%10" CYCLES_FMT ", "
				"%10" CYCLES_FMT ", "
				"%10" CYCLES_FMT ", "
				"%12lu",
				counter++, write_cycle,
				wss, delay, last_cpu, next_cpu, cold,
				hot1, hot2, hot3,
				after_resume,
				(unsigned long) mem);
			get_phys_addrs(0,
				(unsigned long) mem,
				wss * 1024 + (unsigned long) mem,
				phys_addrs,
				wss);
			for (i = 0; i < num_pages; i++)
				fprintf(outfile, ", %12lu", phys_addrs[i]);
			fprintf(outfile, "\n");
		}
		if (next_cpu == last_cpu)
			preempt_counter++;
		else
			migration_counter++;
		last_cpu = next_cpu;
		deallocate(mem);
	}
	free(phys_addrs);
}

static void on_sigalarm(int signo)
{
	/*fprintf(stderr, "SIGALARM\n");*/
	exit(0);
}


static void usage(char *error) {
	if (error)
		fprintf(stderr, "Error: %s\n", error);
	fprintf(stderr,
"Usage: cache_cost [-m PROCS] [-w WRITECYCLE] [-s WSS] [-x MINIMUM SLEEP TIME]\n"
"                  [-y MAXIMUM SLEEP TIME] [-n] [-c SAMPLES] [-l DURATION] \n"
"                  [-o FILENAME] [-h] [-b] [-R REPETITIONS]\n"
"Options:\n"
"       -b: Run as a best-effort task (for debugging, NOT for measurements)\n"
"       -m: Enable migrations among the first PROCS processors. \n"
"           Example: -m2 means migrations between processors 0 and 1. \n"
"           Omit to consider only preemptions.\n"
"       -w: (1/WRITECYCLE) is the proportion of writes, \n"
"           Example: WRITECYCLE = 3 means that 1/3 of the operations are writes.\n"
"           Use 0 for read-only.\n"
"       -s: WSS size in kB.\n"
"       -x: Minimum sleep time between preemptions/migrations.\n"
"       -y: Maximum sleep time between preemptions/migrations.\n"
"       -n: Automatically name output files.\n"
"       -c: Number of generated samples of preemptions and migrations.\n"
"       -l: Duration of the execution in seconds.\n"
"       -o: Name of output file.\n"
"       -R: repeat the experiment several times\n"
"       -h: Show this message.\n");
	exit(1);
}


#define OPTSTR "m:w:l:s:o:x:y:nc:hbR:"

int main(int argc, char** argv)
{
	int num_cpus = 1; /* only do preemptions by default */
	int wss = 64;     /* working set size, in kb */
	int sleep_min = 0;
	int sleep_max = 1000;
	int exit_after = 0; /* seconds */
	int write_cycle = 0; /* every nth cycle is a write; 0 means read-only  */
	FILE* out = stdout;
	char fname[255];
	struct utsname utsname;
	int auto_name_file = 0;
	int sample_count = 0;
	int opt;
	int best_effort = 0;
	int repetitions = 1;
	int i;

	srand (time(NULL));

	while ((opt = getopt(argc, argv, OPTSTR)) != -1) {
		switch (opt) {
		case 'm':
			num_cpus = atoi(optarg);
			break;
		case 'c':
			sample_count = atoi(optarg);
			break;
		case 's':
			wss = atoi(optarg);
			break;
		case 'w':
			write_cycle = atoi(optarg);
			break;
		case 'l':
			exit_after = atoi(optarg);
			break;
		case 'o':
			out = fopen(optarg, "w");
			if (out == NULL)
				usage("could not open file");
			break;
		case 'n':
			auto_name_file = 1;
			break;
		case 'x':
			sleep_min = atoi(optarg);
			break;
		case 'y':
			sleep_max = atoi(optarg);
			break;
		case 'b':
			best_effort = 1;
			break;
		case 'R':
			repetitions = atoi(optarg);
			if (repetitions <= 0)
				usage("invalid number of repetitions");
			break;
		case 'h':
			usage(NULL);
			break;
		case ':':
			usage("Argument missing.");
			break;
		case '?':
		default:
			usage("Bad argument.");
			break;
		}
	}

	if (num_cpus <= 0)
		usage("Number of CPUs must be positive.");

	if (wss <= 0)
		usage("The working set size must be positive.");

	if (sleep_min < 0 || sleep_min > sleep_max)
		usage("Invalid minimum sleep time");

	if (write_cycle < 0)
		usage("Write cycle may not be negative.");

	if (sample_count < 0)
		usage("Sample count may not be negative.");

	if (check_migrations(num_cpus) != 0)
		usage("Invalid CPU range.");

	if (!best_effort && become_posix_realtime_task() != 0)
		die("Could not become real-time task.");

	if (!best_effort && lock_memory() != 0)
		die("Could not lock memory.");

	if (auto_name_file) {
		uname(&utsname);
		snprintf(fname, 255,
			 "pmo_host=%s_wss=%d_wcycle=%d_smin=%d_smax=%d.csv",
			 utsname.nodename, wss, write_cycle, sleep_min, sleep_max);
		out = fopen(fname, "w");
		if (out == NULL) {
			fprintf(stderr, "Can't open %s.", fname);
			die("I/O");
		}
	}

	if (exit_after > 0) {
		signal(SIGALRM, on_sigalarm);
		alarm(exit_after);
	}

	if (best_effort) {
		fprintf(out, "\n[!!!] WARNING: running in best-effort mode "
		             "=> all measurements are unreliable!\n\n");
	}


	for (i = 0; i < repetitions; i++)
		do_random_experiment(out,
			             num_cpus, wss, sleep_min,
			             sleep_max, write_cycle,
			             sample_count,
			             best_effort);
	fclose(out);
	return 0;
}
