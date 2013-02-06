/*
 * Author: Christopher R. Palmer <crpalmer@gmail.com>
 *
 * Driver framework copied from Paul Reioux aka Faux123 <reioux@gmail.com>'s
 * intelli_plug.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/earlysuspend.h>
#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/module.h>

#define SIMPLE_PLUG_MAJOR_VERSION	1
#define SIMPLE_PLUG_MINOR_VERSION	0

#define DEF_SAMPLING_MS			(10)
#define HISTORY_SIZE			10
#define NUM_CORES			4

struct delayed_work simple_plug_work;

static unsigned int simple_plug_active = 1;
static unsigned int min_cores = 1;
static unsigned int max_cores = NUM_CORES;
static unsigned int sampling_ms = DEF_SAMPLING_MS;
static unsigned int reset_stats;

static unsigned int nr_avg;
static unsigned int time_cores_running[NUM_CORES];
static unsigned int times_core_up[NUM_CORES];
static unsigned int times_core_down[NUM_CORES];
static unsigned int nr_run_history[HISTORY_SIZE];
static unsigned int nr_last_i;

module_param(simple_plug_active, uint, 0644);
module_param(min_cores, uint, 0644);
module_param(max_cores, uint, 0644);
module_param(sampling_ms, uint, 0644);
module_param(reset_stats, uint, 0644);

module_param(nr_avg, uint, 0444);
module_param_array(time_cores_running, uint, NULL, 0444);
module_param_array(times_core_up, uint, NULL, 0444);
module_param_array(times_core_down, uint, NULL, 0444);
module_param_array(nr_run_history, uint, NULL, 0444);

static unsigned int calculate_thread_stats(void)
{
	int target_cores;

	nr_avg -= nr_run_history[nr_last_i];
	nr_avg += nr_run_history[nr_last_i] = avg_nr_running();
	nr_last_i = (nr_last_i + 1) % HISTORY_SIZE;

	target_cores = ((nr_avg>>FSHIFT) / HISTORY_SIZE);

	if (target_cores > max_cores)
		return max_cores;
	else if (target_cores < min_cores)
		return min_cores;
	else
		return target_cores;
}

static void
cpus_up_down(int nr_run_stat)
{
	int n_online = num_online_cpus();

	BUG_ON(nr_run_stat < 1 || nr_run_stat > NUM_CORES);

	time_cores_running[nr_run_stat-1]++;

	while(n_online < nr_run_stat) {
		times_core_up[n_online]++;
		cpu_up(n_online);
		n_online++;
	}

	while(n_online > nr_run_stat) {
		n_online--;
		times_core_down[n_online]++;
		cpu_down(n_online);
	}
}
     
static void __cpuinit simple_plug_work_fn(struct work_struct *work)
{
	if (reset_stats) {
		reset_stats = 0;
		memset(time_cores_running, 0, sizeof(time_cores_running));
		memset(times_core_up, 0, sizeof(times_core_up));
		memset(times_core_down, 0, sizeof(times_core_down));
	}
	
	if (simple_plug_active == 1) {
		unsigned int nr_run_stat = calculate_thread_stats();
		cpus_up_down(nr_run_stat);
	}

	schedule_delayed_work_on(0, &simple_plug_work,
		msecs_to_jiffies(sampling_ms));
}


#ifdef CONFIG_HAS_EARLYSUSPEND
static void simple_plug_early_suspend(struct early_suspend *handler)
{
	int i;
	
	cancel_delayed_work_sync(&simple_plug_work);

	// put rest of the cores to sleep!
	for (i=1; i < NUM_CORES; i++) {
		if (cpu_online(i))
			cpu_down(i);
	}
}

static void __cpuinit simple_plug_late_resume(struct early_suspend *handler)
{
	int i;
	unsigned almost_2 = (2 << FSHIFT) - 1;

	/* setup a state which will let a second core come online very
	 * easily if there is much startup load (which there usually is)
	 */

	for (i = 0; i < HISTORY_SIZE; i++) {
		nr_run_history[i] = almost_2;
	}
	nr_avg = almost_2 * HISTORY_SIZE;

	
	/* Ask it to run very soon to allow that ramp-up to happen */

	schedule_delayed_work_on(0, &simple_plug_work,
		msecs_to_jiffies(1));
}

static struct early_suspend simple_plug_early_suspend_struct_driver = {
	.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 10,
	.suspend = simple_plug_early_suspend,
	.resume = simple_plug_late_resume,
};
#endif	/* CONFIG_HAS_EARLYSUSPEND */

int __init simple_plug_init(void)
{
	pr_info("simple_plug: version %d.%d by crpalmer\n",
		 SIMPLE_PLUG_MAJOR_VERSION,
		 SIMPLE_PLUG_MINOR_VERSION);

	INIT_DELAYED_WORK(&simple_plug_work, simple_plug_work_fn);
	schedule_delayed_work_on(0, &simple_plug_work, msecs_to_jiffies(sampling_ms));

#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&simple_plug_early_suspend_struct_driver);
#endif
	return 0;
}

MODULE_AUTHOR("Christopher R. Palmer <crpalmer@gmail.com>");
MODULE_DESCRIPTION("'simple_plug' - An simple cpu hotplug driver for "
	"Low Latency Frequency Transition capable processors");
MODULE_LICENSE("GPL");

late_initcall(simple_plug_init);
