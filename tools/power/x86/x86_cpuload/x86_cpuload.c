/*
 * x86_cpuload.c
 *
 * Copyright (C) 2015 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <getopt.h>
#include <unistd.h>
#include <pthread.h>

struct cpu_info {
	unsigned long long sample_tsc;
	unsigned long long timeout_tsc;
	pthread_t thread;
};
struct cpu_info *cpu_info;

/* To simulate the load, sleep for (100-load)msecs during each sample. */
#define DEFAULT_SAMPLE_MS 10
#define DEFAULT_TIMEOUT_SEC 10
#define DEFAULT_CPU_LOAD 100
static int sample_ms = DEFAULT_SAMPLE_MS;
static int time_out = DEFAULT_TIMEOUT_SEC;
static int work_load = DEFAULT_CPU_LOAD;
static unsigned long thread_count;
static unsigned long start_cpu;
static unsigned long online_cpus;
static char *progname;

static void err_exit(char *msg)
{
	perror(msg);
	exit(1);
}

static unsigned long get_online_cpus(void)
{
	FILE *fp;
	unsigned long cpu_num;
	int cpu_id, ret;
	char *proc_stat = "/proc/stat";

	fp = fopen(proc_stat, "r");
	if (!fp)
		err_exit("Open failed /proc/stat");

	ret = fscanf(fp, "cpu %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d\n");
	if (ret)
		err_exit("Failed to parse format");

	while (1) {
		ret = fscanf(fp, "cpu%u %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d\n", &cpu_id);
		if (ret != 1)
			break;
		cpu_num++;
	}
	fclose(fp);

	return cpu_num;
}

static unsigned long long rdtsc(void)
{
	unsigned int low, high;

	asm volatile("rdtsc" : "=a" (low), "=d" (high));

	return low | ((unsigned long long)high) << 32;
}

/* To estimate the delta tsc for sample_ms and time_out */
static int cpu_info_init(unsigned long cpu)
{
	unsigned long long sample_tsc;

	sample_tsc = rdtsc();
	usleep(sample_ms * 1000);
	sample_tsc = cpu_info[cpu].sample_tsc = rdtsc() - sample_tsc;
	cpu_info[cpu].timeout_tsc =
		 time_out*1000000*sample_tsc/sample_ms/1000;

	return 0;
}

static void cpus_init(void)
{
	online_cpus = get_online_cpus();
	if (!online_cpus)
		err_exit("Get cpu online number failed");

	cpu_info = malloc(sizeof(struct cpu_info) * online_cpus);
	if (!cpu_info)
		err_exit("Allocate cpu_info array failed");
}

/**
 * consume() - run specific time during sample period
 * @sleep_us: usec to sleep during one sample
 * @sample_tsc: tsc increment during one sample
 * @timeout_tsc: tsc increment duing whole period
 *
 * simulate the load by sleep/sample_tsc, which lasts for 'timeout_tsc'
 *
 */
static void consume(unsigned long long sleep_us, unsigned long long sample_tsc,
		unsigned long long timeout_tsc)
{
	unsigned long long end, now, end_sample;

	now = rdtsc();
	end = now + timeout_tsc;
	end_sample = now + sample_tsc;
	while (now < end) {
		now = rdtsc();
		if (now > end_sample) {
			usleep(sleep_us);
			end_sample += sample_tsc;
		}
	}
}

static unsigned long long get_percent(unsigned long long src,
				     unsigned int percent)
{
	return (src * percent / 100);
}

void *cpu_workload(void *arg)
{
	unsigned long long usec_sleep, sample_tsc, timeout_tsc;
	unsigned long cpu = (unsigned long)arg;

	if (cpu_info_init(cpu)) {
		perror("cpu_info_init failed");
		return 0;
	}
	sample_tsc = cpu_info[cpu].sample_tsc;
	timeout_tsc = cpu_info[cpu].timeout_tsc;
	usec_sleep = sample_ms * 1000 - get_percent(sample_ms * 1000, work_load);
	consume(usec_sleep, sample_tsc, timeout_tsc);

	return 0;
}

static void start_worker_threads(void)
{
	unsigned long i, end;
	int ret;
	cpu_set_t cpus;
	pthread_attr_t thread_attr;

	pthread_attr_init(&thread_attr);
	i = start_cpu;
	end = i + thread_count;
	for (; i < end; ++i) {
		CPU_ZERO(&cpus);
		CPU_SET(i, &cpus);
		ret = pthread_attr_setaffinity_np(&thread_attr, sizeof(cpu_set_t), &cpus);
		if (ret)
			err_exit("pthread_attr_setaffinity_np failed");
		printf("Starting workload on cpu %ld with load %d%%, lasts for %d seconds...\n",
			i, work_load, time_out);
		pthread_create(&cpu_info[i].thread, &thread_attr, cpu_workload,
				(void *)(unsigned long)i);
	}
	pthread_attr_destroy(&thread_attr);
	/* Wait for the threads to be scheduled. */
	sleep(1);
	for (i = start_cpu; i < end; i++) {
		ret = pthread_join(cpu_info[i].thread, NULL);
		if (ret)
			err_exit("pthread_join failed\n");
	}
	printf("Done.\n");
}

static const struct option long_options[] = {
	/* These options set a flag. */
	{"start", required_argument, NULL, 's'},
	{"thread", required_argument, NULL, 'c'},
	{"time", required_argument, NULL, 't'},
	{"load", required_argument, NULL, 'l'},
	{0, 0, 0, 0}
};

void help(void)
{
	/* Bind load on cpus from start_cpu to start_cpu+thread_count*/
	printf("%s: [-s start_cpuid][-c thread_count][-t second][-l load_percent]\n", progname);
	exit(1);
}

void cmdline(int argc, char **argv)
{
	int opt, option_index = 0;

	progname = argv[0];

	while ((opt = getopt_long(argc, argv, "c:l:t:s:p:h", long_options, &option_index)) != -1) {
		switch (opt) {
		case 's':
			start_cpu = atol(optarg);
			break;
		case 'c':
			thread_count = atol(optarg);
			break;
		case 't':
			time_out = atoi(optarg);
			break;
		case 'l':
			work_load = atoi(optarg);
			break;
		case 'p':
			sample_ms = atoi(optarg);
			break;
		case 'h':
		default:
			help();
		}
	}
}

static int verify_param(void)
{
	return (start_cpu < online_cpus) &&
		(thread_count > 0 && thread_count <= online_cpus) &&
		(start_cpu + thread_count <= online_cpus);
}

int main(int argc, char *argv[])
{
	cmdline(argc, argv);
	cpus_init();
	if (!verify_param()) {
		help();
		return -1;
	}

	start_worker_threads();

	return 0;
}

