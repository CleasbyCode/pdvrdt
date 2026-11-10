#pragma once

// Worker threads for the stages that split cleanly: the Reddit carrier's
// embed, extract and PNG encode, and the deflate of large payloads.

#include <sched.h>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

// Threads the parallel stages use: the CPUs this process may run on
// (its affinity mask, not the machine's total), capped where the memory-bound
// stages stop gaining.
[[nodiscard]] inline unsigned workerCount() noexcept {
	constexpr unsigned MAX_WORKERS = 16;
	cpu_set_t set;
	CPU_ZERO(&set);
	unsigned count = 0;
	if (sched_getaffinity(0, sizeof(set), &set) == 0) {
		count = static_cast<unsigned>(CPU_COUNT(&set));
	}
	if (count == 0) {
		count = std::thread::hardware_concurrency();
	}
	return std::clamp(count, 1U, MAX_WORKERS);
}

// Runs body(begin, end) over [0, count) split into contiguous ranges, one per
// thread, on at most `threads` threads (the caller is one of them). Ranges are
// at least `min_per_thread` long, so small jobs stay on the calling thread.
// The first exception any range throws is rethrown here after all have
// finished.
template <typename Body>
void parallelFor(std::size_t count, std::size_t min_per_thread, unsigned threads, Body&& body) {
	if (count == 0) {
		return;
	}
	const std::size_t by_size = std::max<std::size_t>(1, count / std::max<std::size_t>(1, min_per_thread));
	const std::size_t parts = std::min<std::size_t>(std::max(1U, threads), by_size);
	if (parts <= 1) {
		body(std::size_t{0}, count);
		return;
	}
	std::exception_ptr failure;
	std::mutex failure_mutex;
	const auto run = [&](std::size_t part) noexcept {
		const std::size_t begin = count * part / parts;
		const std::size_t end = count * (part + 1) / parts;
		try {
			body(begin, end);
		} catch (...) {
			const std::scoped_lock lock(failure_mutex);
			if (!failure) {
				failure = std::current_exception();
			}
		}
	};
	{
		std::vector<std::jthread> workers;
		workers.reserve(parts - 1);
		for (std::size_t part = 1; part < parts; ++part) {
			workers.emplace_back(run, part);
		}
		run(0);
	}
	if (failure) {
		std::rethrow_exception(failure);
	}
}
