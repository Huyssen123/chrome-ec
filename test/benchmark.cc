/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Simple test to validate the benchmark library.
 */

#include "benchmark.h"

extern "C" {
#include "test_util.h"
}

/* Sample function for the benchmark */
static void float_mult()
{
	float a = 1.1;
	float b = 1.1;
	int i;

	for (i = 0; i < 1000; ++i)
		a *= b;
}

test_static int test_valid_benchmark()
{
	Benchmark benchmark;

	auto result = benchmark.run("float_mult", float_mult);
	TEST_ASSERT(result.has_value());

	benchmark.print_results();
	return EC_SUCCESS;
}

test_static int test_num_iterations()
{
	Benchmark benchmark({ .num_iterations = 5 });
	int num_calls = 0;

	auto result = benchmark.run("call_counter", [&] { ++num_calls; });
	TEST_ASSERT(result.has_value());
	TEST_EQ(num_calls, 5, "%d");

	return EC_SUCCESS;
}

test_static int test_empty_benchmark_name()
{
	Benchmark benchmark;
	TEST_ASSERT(!benchmark.run("", [] {}).has_value());
	return EC_SUCCESS;
}

test_static int test_too_many_runs()
{
	auto benchmark = Benchmark<3>();
	TEST_ASSERT(benchmark.run("call_1", [] {}).has_value());
	TEST_ASSERT(benchmark.run("call_2", [] {}).has_value());
	TEST_ASSERT(benchmark.run("call_3", [] {}).has_value());
	TEST_ASSERT(!benchmark.run("call_4", [] {}).has_value());

	return EC_SUCCESS;
}

test_static int test_min_max_time()
{
	// Run test 3 times with increasing delay of 1ms, 2ms, and 4ms
	Benchmark benchmark({ .num_iterations = 3 });
	int delay_us = 1000;

	auto result = benchmark.run("delay", [&delay_us] {
		udelay(delay_us);
		delay_us *= 2;
	});
	TEST_ASSERT(result.has_value());

	auto min_time = result.value().min_time;
	auto max_time = result.value().max_time;

	TEST_GE(min_time, 995U, "%u");
	TEST_LE(min_time, 1005U, "%u");
	TEST_GE(max_time, 3995U, "%u");
	TEST_LE(max_time, 4005U, "%u");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	RUN_TEST(test_valid_benchmark);
	RUN_TEST(test_num_iterations);
	RUN_TEST(test_too_many_runs);
	RUN_TEST(test_empty_benchmark_name);
	RUN_TEST(test_min_max_time);
	test_print_result();
}
