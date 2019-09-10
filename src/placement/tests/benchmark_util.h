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

#else /* USE_TIME_PROFILING */

#define BENCHMARK_START(max_current_time_count_in)
#define BENCHMARK_STOP(wallclock_delta_ns, thread_delta_ns)
#define BENCHMARK_FOR_EACH_TIME(benchmark_idx, wallclock_delta_ns, thread_delta_ns)
#define BENCHMARK_LOG_CURRENT_TIME(key)

#endif /* USE_TIME_PROFILING */

#endif /* __TIME_PROFILING_H__ */


