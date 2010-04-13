/*
 * pm_task.c
 *
 * A real-time task that measures preemption and migration costs
 * for a specific working set size.
 *
 * 2008		Original version and idea by John Calandrino
 * 		(litmus2008, liblitmus2008)
 *
 * 2010		Porting of original program to litmus2010 and
 * 		integration within liblitmus2010 by Andrea Bastoni
 */

/* common data structures and defines */
#include "pm_common.h"

#include "litmus.h"
#include "asm.h"
#include "cycles.h"

/* architectural dependend code for pm measurement */
#include "pm_arch.h"

#include <sys/io.h>

int mem_block[NUMWS][INTS_PER_WSS] __attribute__ ((aligned(CACHEALIGNMENT)));

/* Setup flags, then enter loop to measure costs. */
int main(int argc, char **argv)
{
	/* control_page to read data from kernel */
	struct control_page *ctrl = NULL;
#ifdef DEBUG
	struct control_page *saved_ctrl_page_ptr = 0;
#endif

	unsigned long curr_job_count = 0;
	unsigned long curr_sched_count = 0;
	unsigned int curr_cpu = 0;
	unsigned long curr_last_rt_task = 0;
	unsigned long curr_ws = 0;

	unsigned long long curr_preemption_length = 0;

	unsigned long long start_time, end_time;

	int *mem_ptr = NULL;
	int *mem_ptr_end = NULL;

	struct data_entry data_points[DATAPOINTS];
	int data_count = 0;
	int data_wrapped = 0;

	int refcount;

	int task_pid = gettid();
	int task_period;
	int read, *loc_ptr;
	struct rt_task param;

	char *filename;
#ifdef DEBUG
	int i;
#endif

	if (argc < 2) {
		printf("pm_task: need a filename\n");
		return -1;
	}

	filename = argv[1];
#ifdef DEBUG
	fprintf(stderr, "Saving on %s\n",filename);
#endif

	/* Initialize random library for read/write ratio enforcement. */
	srandom(SEEDVAL);

	/* this will lock all pages and will call init_kernel_iface */
	init_litmus();

	/* Ensure that the pages that we care about, either because they
	 * are shared with the kernel or they are performance-critical,
	 * are loaded and locked in memory before benchmarking begins.
	 */
	memset(&param, 0, sizeof(struct rt_task));

	memset(&mem_block, 0, sizeof(int) * NUMWS * INTS_PER_WSS);

	memset(&mem_ptr, 0, sizeof(int*));
	memset(&mem_ptr_end, 0, sizeof(int*));

	/* Get task period. */
	if (get_rt_task_param(task_pid, &param) < 0) {
		perror("Cannot get task parameters\n");
		return -1;
	}

	task_period = param.period / NS_PER_MS;

	/* get the shared control page for this task */
	if (!(ctrl = get_ctrl_page())) {
		perror("Cannot get the shared control page\n");
		return -1;
	}
#ifdef DEBUG
	saved_ctrl_page_ptr = ctrl;
#endif

	/* Enter loop that measures preemption and migration costs. */
	while (curr_job_count * task_period < SIMRUNTIME) {
#ifdef DEBUG
		/* try to understand if bad bad things happened */
		if(ctrl != saved_ctrl_page_ptr) {
			fprintf(stderr, "BAD BAD BAD!\n\nCtrl page changed! %p != %p\n",
					saved_ctrl_page_ptr, ctrl);
			return -1;
		}
#endif
		if (curr_job_count != ctrl->job_count) {

			/* ok, this is a new job. Get info from kernel */

			curr_job_count = ctrl->job_count;
			curr_sched_count = ctrl->sched_count;
			curr_cpu = ctrl->cpu;
			curr_last_rt_task = ctrl->last_rt_task;

			barrier();

			/* job's portion of the mem_block */
			curr_ws = curr_job_count % NUMWS;

			mem_ptr = &mem_block[curr_ws][0];
			mem_ptr_end = mem_ptr + INTS_PER_WSS;

			/* Access WS when cache cold, then immediately
			 * re-access to calculate "cache-hot" access time.
			 */

			/* Cache-cold accesses. */
			start_time = get_cycles();
			for (; mem_ptr < mem_ptr_end; mem_ptr += 1024)
				readwrite_one_thousand_ints(mem_ptr);
			end_time = get_cycles();

                        data_points[data_count].timestamp = end_time;

			/* Am I the same I was before? */
			if (curr_job_count != ctrl->job_count ||
					curr_sched_count != ctrl->sched_count ||
					curr_cpu != ctrl->cpu)
				/* fishiness */
				data_points[data_count].access_type = 'c';
			else
				/* okay */
				data_points[data_count].access_type = 'C';

			data_points[data_count].access_time =
				end_time - start_time;
			data_points[data_count].cpu = curr_cpu;
			data_points[data_count].job_count = curr_job_count;
			data_points[data_count].sched_count = curr_sched_count;
			data_points[data_count].last_rt_task = curr_last_rt_task;
			data_points[data_count].preemption_length = 0;

                        data_wrapped = ((data_count+1) / DATAPOINTS > 0);
                        data_count = (data_count+1) % DATAPOINTS;

			barrier();

			/* "Best case". Read multiple times. */
			for (refcount = 0; refcount < REFTOTAL; refcount++) {

				mem_ptr = &mem_block[curr_ws][0];

				start_time = get_cycles();
				for (; mem_ptr < mem_ptr_end; mem_ptr += 1024)
					readwrite_one_thousand_ints(mem_ptr);
				end_time = get_cycles();

				data_points[data_count].timestamp = end_time;

				if (curr_job_count != ctrl->job_count ||
				   curr_sched_count != ctrl->sched_count ||
				   curr_cpu != ctrl->cpu)
					/* fishiness */
					data_points[data_count].
						access_type = 'h';
				else
					/* okay */
					data_points[data_count].
						access_type = 'H';

				data_points[data_count].access_time =
					end_time - start_time;
				data_points[data_count].cpu = curr_cpu;
				data_points[data_count].job_count =
					curr_job_count;
				data_points[data_count].sched_count =
					curr_sched_count;
				data_points[data_count].last_rt_task =
					curr_last_rt_task;
				data_points[data_count].preemption_length = 0;

				data_wrapped =
					((data_count+1) / DATAPOINTS > 0);
				data_count = (data_count+1) % DATAPOINTS;
			}

		} else if (mem_ptr && mem_ptr_end &&
			   (curr_sched_count != ctrl->sched_count ||
			    curr_cpu != ctrl->cpu)) {

			/* we have done at least one go in the "best case".
			 * job is the same => preempted / migrated
			 */
			curr_preemption_length =
				ctrl->preempt_end - ctrl->preempt_start;
			curr_job_count = ctrl->job_count;
			curr_sched_count = ctrl->sched_count;
			curr_cpu = ctrl->cpu;
			curr_last_rt_task = ctrl->last_rt_task;

			barrier();

			/* Measure preemption or migration cost. */
			mem_ptr = &mem_block[curr_ws][0];

			start_time = get_cycles();
			for (; mem_ptr < mem_ptr_end; mem_ptr += 1024)
				readwrite_one_thousand_ints(mem_ptr);
			end_time = get_cycles();

			data_points[data_count].timestamp = end_time;

			/* just record pP, we tell the difference later */
                        if (curr_job_count != ctrl->job_count ||
                            curr_sched_count != ctrl->sched_count ||
                            curr_cpu != ctrl->cpu)
				/* fishiness */
                                data_points[data_count].access_type = 'p';
                        else
                                /* okay */
                                data_points[data_count].access_type = 'P';

                        data_points[data_count].access_time =
                                                        end_time - start_time;
                        data_points[data_count].cpu = curr_cpu;
                        data_points[data_count].job_count = curr_job_count;
                        data_points[data_count].sched_count = curr_sched_count;
			data_points[data_count].last_rt_task =
				curr_last_rt_task;
			data_points[data_count].preemption_length =
				curr_preemption_length;

			data_wrapped = ((data_count+1) / DATAPOINTS > 0);
			data_count = (data_count+1) % DATAPOINTS;

		} else if (mem_ptr && mem_ptr_end) {
			/*
			 * Ok, we run (and we wait for a p/m event):
			 * Read or write some random location in the WS
			 * to keep the task "cache warm". We only do
			 * this if the pointers are valid, because we
			 * do not want to skew the "cold" read of the WS
			 * on the first job.
			 */
			read = (random() % 100) < READRATIO;
			loc_ptr = &mem_block[curr_ws][0];
			loc_ptr += (random() % INTS_PER_WSS);

			barrier();

			if (read)
				read_mem(loc_ptr);
			else
				write_mem(loc_ptr);
		}
	}

#ifdef DEBUG
	/* Print (most recent) results. */
	for (i = 0; i < (data_wrapped ? DATAPOINTS : data_count) ; i++)
		fprintf(stderr, "(%c) - ACC %llu, CPU %u, PLEN %llu\n",
		       data_points[i].access_type,
		       data_points[i].access_time, data_points[i].cpu,
		       data_points[i].preemption_length);
#endif
	serialize_data_entry(filename, data_points,
			(data_wrapped ? DATAPOINTS : data_count));

	return 0;
}

