#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sched.h>

#include <sys/io.h>
#include <sys/utsname.h>

#include "litmus.h"
#include "asm.h"

static void die(char *error)
{
	fprintf(stderr, "Error: %s (errno: %m)\n",
		error);
	exit(1);
}

static int check_migrations(int num_cpus)
{
	int cpu, err;

	for (cpu = 0; cpu < num_cpus; cpu++) {
		err = be_migrate_to(cpu);
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
#define ARENA_SIZE_MB 128
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

static int* allocate(int wss)
{
	static int pos = 0;
	int size = wss * INTS_IN_1KB;
	int *mem;

	/* Don't allow re-use between allocations.
	 * At most half of the arena may be used
	 * at any one time.
	 */
	if (size * 2 > ARENA_SIZE)
		die("static memory arena too small");

	if (pos + size > ARENA_SIZE) {
		/* wrap to beginning */
		mem = arena;
		pos = size;
	} else {
		mem = arena + pos;
		pos += size;
	}

	return mem;
}

static void deallocate(int *mem)
{
}

static void migrate_to(int target)
{
	if (be_migrate_to(target) != 0)
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

static void do_random_experiment(FILE* outfile,
				 int num_cpus, int wss,
				 int sleep_min, int sleep_max,
				 int write_cycle, int sample_count)
{
	int last_cpu, next_cpu, delay;
	unsigned long counter = 1;

	cycles_t start, stop;
	cycles_t cold, hot1, hot2, hot3, after_resume;

	int *mem;

	migrate_to(0);
	last_cpu = 0;

	/* prefault and dirty cache */
	touch_arena();


	iopl(3);
	while (!sample_count || sample_count >= counter) {
		delay = sleep_min + random() % (sleep_max - sleep_min + 1);
		next_cpu = random() % num_cpus;

		mem = allocate(wss);

		cli();
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
		sti();

		migrate_to(next_cpu);
		sleep_us(delay);

		cli();
		start = get_cycles();
		mem[0] = touch_mem(mem, wss, write_cycle);
		stop  = get_cycles();
		sti();
		after_resume = stop - start;

		/* run, write ratio, wss, delay, from, to, cold, hot1, hot2,
		 * hot3, after_resume */
		fprintf(outfile,
			"%6ld, %3d, %6d, %6d, %3d, %3d, "
			"%" CYCLES_FMT ", "
			"%" CYCLES_FMT ", "
			"%" CYCLES_FMT ", "
			"%" CYCLES_FMT ", "
			"%" CYCLES_FMT "\n",
			counter++, write_cycle,
			wss, delay, last_cpu, next_cpu, cold, hot1, hot2, hot3,
			after_resume);
		last_cpu = next_cpu;
		deallocate(mem);
	}
}

static void on_sigalarm(int signo)
{
	/*fprintf(stderr, "SIGALARM\n");*/
	exit(0);
}

static void usage(char *error) {
	/* TODO: actually provide usage instructions */
	die(error);
}


#define OPTSTR "m:w:l:s:o:x:y:nc:"

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

	if (become_posix_realtime_task() != 0)
		die("Could not become realt-time task.");

	if (lock_memory() != 0)
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

	do_random_experiment(out,
			     num_cpus, wss, sleep_min,
			     sleep_max, write_cycle,
			     sample_count);
	fclose(out);
	return 0;
}
