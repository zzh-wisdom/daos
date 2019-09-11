/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2016 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __TIME_PROFILING_H__
#define __TIME_PROFILING_H__

#ifdef USE_TIME_PROFILING

#include <stdio.h>
#include <time.h>
#include <float.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#define COMPILER_BARRIER() asm volatile("" ::: "memory")

/**
 * Number of nanoseconds in one second.
 */
#define NANOSECONDS_PER_SECOND 1000000000LL

/**
 * Starts a benchmark
 *  \param max_current_time_count_in Maximum number of times BENCHMARK_LOG_CURRENT_TIME() can be invoked before STOP is called
 */
#define BENCHMARK_START(max_current_time_count_in) \
	struct timespec wallclock_start_time, thread_start_time; \
	__attribute__((unused)) struct timespec wallclock_step_times[max_current_time_count_in], thread_step_times[max_current_time_count_in]; \
	__attribute__((unused)) char *benchmark_step_labels[max_current_time_count_in]; \
	__attribute__((unused)) int benchmark_num_time_entries = 0; \
	__attribute__((unused)) int benchmark_max_current_time_count = max_current_time_count_in; \
	COMPILER_BARRIER(); \
	(void)clock_gettime( CLOCK_MONOTONIC, &wallclock_start_time ); \
	(void)clock_gettime( CLOCK_THREAD_CPUTIME_ID, &thread_start_time ); \
	COMPILER_BARRIER()

/**
 * Stops a benchmark and declares local variables to access the results
 */
#define BENCHMARK_STOP(wallclock_delta_ns, thread_delta_ns) \
	struct timespec wallclock_stop_time, thread_stop_time; \
	COMPILER_BARRIER(); \
	(void)clock_gettime( CLOCK_MONOTONIC, &wallclock_stop_time ); \
	(void)clock_gettime( CLOCK_THREAD_CPUTIME_ID, &thread_stop_time ); \
	long long (wallclock_delta_ns) = ( ( (long long)( wallclock_stop_time.tv_sec - wallclock_start_time.tv_sec ) * NANOSECONDS_PER_SECOND ) + (long long)( wallclock_stop_time.tv_nsec - wallclock_start_time.tv_nsec ) ); \
	long long (thread_delta_ns) = ( ( (long long)( thread_stop_time.tv_sec - thread_start_time.tv_sec ) * NANOSECONDS_PER_SECOND ) + (long long)( thread_stop_time.tv_nsec - thread_start_time.tv_nsec ) );


/* TODO: This is broken and not working yet */
#define BENCHMARK_FOR_EACH_TIME(benchmark_idx, wallclock_delta_ns, thread_delta_ns) \
	for (long long (benchmark_idx) = 0, \
		(wallclock_delta_ns) = ( ( (long long)( wallclock_step_times[benchmark_idx].tv_sec - wallclock_start_time.tv_sec ) * NANOSECONDS_PER_SECOND ) + (long long)( wallclock_step_times[benchmark_idx].tv_nsec - wallclock_start_time.tv_nsec ) ), \
		(thread_delta_ns) = ( ( (long long)( thread_step_times[benchmark_idx].tv_sec - thread_start_time.tv_sec ) * NANOSECONDS_PER_SECOND ) + (long long)( thread_step_times[benchmark_idx].tv_nsec - thread_start_time.tv_nsec ) ) \
		; (benchmark_idx) < benchmark_num_time_entries; (benchmark_idx)++)

/**
 * Writes the current value of the realtime clock to a temporary buffer (fast)
 * \param key String to use to identify data from this iteration
 */
#define BENCHMARK_LOG_CURRENT_TIME(key) \
	COMPILER_BARRIER(); \
	if (benchmark_num_time_entries < benchmark_max_current_time_count) { \
		(void)clock_gettime( CLOCK_MONOTONIC, &wallclock_step_times[benchmark_num_time_entries] ); \
		(void)clock_gettime( CLOCK_THREAD_CPUTIME_ID, &thread_step_times[benchmark_num_time_entries] ); \
		benchmark_step_labels[benchmark_num_time_entries] = key; \
		benchmark_num_time_entries++; \
	} \
	COMPILER_BARRIER()

static inline void BENCHMARK_GRAPH(double *ydata, const char **const keys,
                                   int64_t series_count, int64_t data_count,
				   const char *xlabel, const char *ylabel,
				   double y_user_max, const char *title,
				   const char *fifo_path, const bool use_x11)
{
	FILE  *gp_w;
	FILE  *gp_r;
	int64_t idx = 0;
	int64_t series = 0;
	double ydata_max = 0, ydata_min = DBL_MAX;

	/* Create a FIFO to communicate with gnuplot, unless it already exists */
	if (mkfifo(fifo_path, 0600)) {
		if (errno != EEXIST) {
			perror(fifo_path);
			unlink(fifo_path);
			return;
		}
	}

	gp_w = popen("gnuplot", "w");
	if (gp_w == NULL) {
		perror("popen(gnuplot)");
		pclose(gp_w);
		return;
	}

	/* Tell gnuplot to print to the FIFO */
	fprintf(gp_w, "set print \"%s\"\n", fifo_path);
	fflush(gp_w);

	/* Open the FIFO for reading */
	gp_r = fopen(fifo_path, "r");
	if (gp_r == NULL) {
		perror(fifo_path);
		pclose(gp_w);
		return;
	}

	for (series = 0; series < series_count; series++) {
		for (idx = 0; idx < data_count; idx++) {
			if (ydata[series * data_count + idx] > ydata_max)
				ydata_max = ydata[series * data_count + idx];
			if (ydata[series * data_count + idx] < ydata_min)
				ydata_min = ydata[series * data_count + idx];
		}
	}

	if ((y_user_max) > 0) {
		ydata_max = (y_user_max);
	} else {
		ydata_max *= 1.2F;
	}

	/* Get the terminal width */
	struct winsize w;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

	/* Print the data to gnuplot, which will display the graph */
	if (!use_x11)
		fprintf(gp_w, "set terminal dumb feed %d %d\n",
			w.ws_col - 5, w.ws_row - 5);
	fprintf(gp_w, "set key below vertical\n");
	fprintf(gp_w, "set title \"%s\"\n", title);
	fprintf(gp_w, "set xlabel \"%s\"\n", (xlabel));
	fprintf(gp_w, "set ylabel \"%s\"\n", (ylabel));
	fprintf(gp_w, "set xrange [%d:%ld]\n", 0, data_count);
	fprintf(gp_w, "set yrange [%0f:%0f]\n", 0.0F, ydata_max);
	fprintf(gp_w, "plot \"-\" title \"%s\"", keys[0]);
	for (series = 1; series < series_count; series++)
		fprintf(gp_w, ", \"\" title \"%s\"", keys[series]);
	fprintf(gp_w, "\n");
	for (series = 0; series < series_count; series++) {
		if (series > 0)
			fprintf(gp_w, "e\n");
		for (idx = 0; idx < data_count; idx++) {
			fprintf(gp_w, "%f\n",
				ydata[series * data_count + idx]);
			fprintf(stdout, "%f\n",
				ydata[series * data_count + idx]);
		}
	}
	fprintf(gp_w, "end\n");

	fflush(gp_w);
	fclose(gp_r);
	pclose(gp_w);
	unlink(fifo_path);
}

#else /* USE_TIME_PROFILING */

#define BENCHMARK_START(max_current_time_count_in)
#define BENCHMARK_STOP(wallclock_delta_ns, thread_delta_ns)
#define BENCHMARK_FOR_EACH_TIME(benchmark_idx, wallclock_delta_ns, thread_delta_ns)
#define BENCHMARK_LOG_CURRENT_TIME(key)
#define BENCHMARK_GRAPH(ydata, data_count, xlabel, ylabel, y_user_max, fifo_path)

#endif /* USE_TIME_PROFILING */

#endif /* __TIME_PROFILING_H__ */


