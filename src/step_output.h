#pragma once

// Conceal progress report. Each step prints its label on its own line before
// the work starts (flushed, so a slow step shows what it is waiting on), then
// its result on the next line, indented under a tick, when it ends.

#include <cstddef>
#include <cstdio>
#include <format>
#include <print>
#include <string>
#include <string_view>

#include <sys/ioctl.h>
#include <unistd.h>

inline constexpr std::string_view STEP_DONE = " ✓";
inline constexpr std::string_view STEP_FAILED = " ✗";

inline void beginStep(std::string_view label) {
	std::println("{}...", label);
	(void)std::fflush(stdout);
}

inline void endStep(std::string_view result = {}) {
	if (result.empty()) {
		std::println("{}", STEP_DONE);
	} else {
		std::println("{} {}", STEP_DONE, result);
	}
}

inline void failStep(std::string_view result) {
	std::println("{} {}", STEP_FAILED, result);
	(void)std::fflush(stdout);
}

// A slow step can show a wait note on its result line, on a terminal only (a
// log gets just the result). Returns whether the note was shown; pass that to
// endStepAfterWait(), which moves the cursor back to the start of the note,
// rows included when it wrapped, clears it and prints the result there.
inline constexpr std::string_view WAIT_NOTE =
	" Please wait. Larger data files will take longer to complete this process.";

[[nodiscard]] inline bool showWaitNote(bool needed) {
	if (!needed || isatty(STDOUT_FILENO) == 0) return false;
	std::print("{}", WAIT_NOTE);
	(void)std::fflush(stdout);
	return true;
}

inline void clearWaitNote(bool shown) {
	if (!shown) return;
	std::size_t columns = 80;
	winsize window{};
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == 0 && window.ws_col > 0) {
		columns = window.ws_col;
	}
	const std::size_t rows_up = (WAIT_NOTE.size() - 1) / columns;
	if (rows_up > 0) std::print("\x1b[{}A", rows_up);
	std::print("\r\x1b[J");
}

inline void endStepAfterWait(bool wait_shown, std::string_view result) {
	clearWaitNote(wait_shown);
	endStep(result);
}

namespace step_output_detail {
inline constexpr std::size_t KIB = 1024;
inline constexpr std::size_t MIB = KIB * KIB;

// "784.4" from 7844 tenths; a zero tenth is dropped, so 6170 is "617".
[[nodiscard]] inline std::string tenthsText(std::size_t tenths, std::string_view unit) {
	return tenths % 10 == 0
		? std::format("{}{}", tenths / 10, unit)
		: std::format("{}.{}{}", tenths / 10, tenths % 10, unit);
}
} // namespace step_output_detail

// "843 bytes", "512KiB", "14.2MiB", "3MiB".
[[nodiscard]] inline std::string formatSize(std::size_t bytes) {
	using namespace step_output_detail;
	if (bytes < KIB) return std::format("{} byte{}", bytes, bytes == 1 ? "" : "s");
	if (bytes < MIB) return std::format("{}KiB", bytes / KIB);
	// Tenths rounded half-up in integers, so the Rust port prints the same digits.
	return tenthsText((bytes * 10 + MIB / 2) / MIB, "MiB");
}

// formatSize with one decimal for KiB too, e.g. "784.4KiB" (but "617KiB"), for
// output file sizes. A value that rounds up to 1024KiB is shown as "1MiB".
[[nodiscard]] inline std::string formatSizePrecise(std::size_t bytes) {
	using namespace step_output_detail;
	if (bytes < KIB) return formatSize(bytes);
	const std::size_t kib_tenths = (bytes * 10 + KIB / 2) / KIB;
	if (kib_tenths < KIB * 10) return tenthsText(kib_tenths, "KiB");
	return tenthsText((bytes * 10 + MIB / 2) / MIB, "MiB");
}

// Whole percent, rounded down, as "37%"; a nonzero share under 1% is "<1%".
// Sizes here stay far below SIZE_MAX / 100.
[[nodiscard]] inline std::string percentText(std::size_t part, std::size_t whole) {
	const std::size_t percent = whole == 0 ? 0 : part * 100 / whole;
	return percent == 0 && part > 0 ? std::string("<1%") : std::format("{}%", percent);
}
