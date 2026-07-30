#include "compression.h"
#include "io_utils.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>

namespace {

void writeRepeated(const fs::path& path, Byte value, std::size_t size) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) throw std::runtime_error("unable to create input-snapshot fixture");
	vBytes block(64 * 1024, value);
	while (size != 0) {
		const std::size_t amount = std::min(size, block.size());
		out.write(reinterpret_cast<const char*>(block.data()), static_cast<std::streamsize>(amount));
		if (!out) throw std::runtime_error("unable to write input-snapshot fixture");
		size -= amount;
	}
}

vBytes deflateOpened(const OpenInputFile& file) {
	vBytes compressed;
	zlibDeflateFd(file.fd(), file.size(), Option::None, false, [&](std::span<const Byte> chunk) {
		appendBytes(compressed, chunk, "test compressed-size overflow");
	});
	return compressed;
}

} // namespace

int main(int argc, char** argv) {
	try {
		if (argc != 2 || sodium_init() < 0) return 2;
		const fs::path root(argv[1]);
		fs::create_directories(root);
		constexpr std::size_t FIXTURE_SIZE = 2 * 1024 * 1024;

		const fs::path live = root / "live.bin";
		const fs::path replacement = root / "replacement.bin";
		writeRepeated(live, 0x41, FIXTURE_SIZE);
		writeRepeated(replacement, 0x42, FIXTURE_SIZE);

		OpenInputFile opened = openInputFile(live);
		if (::rename(replacement.c_str(), live.c_str()) != 0) {
			throw std::runtime_error("unable to replace path fixture");
		}
		const vBytes inflated = zlibInflateSpanBounded(deflateOpened(opened), FIXTURE_SIZE);
		if (inflated.size() != FIXTURE_SIZE ||
			!std::ranges::all_of(inflated, [](Byte byte) { return byte == 0x41; })) {
			throw std::runtime_error("compression did not remain bound to the validated descriptor");
		}

		const fs::path growing = root / "growing.bin";
		writeRepeated(growing, 0x43, FIXTURE_SIZE);
		OpenInputFile growth_snapshot = openInputFile(growing);
		const int append_fd = ::open(growing.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC);
		if (append_fd < 0) throw std::runtime_error("unable to open growth fixture");
		const Byte extra = 0x44;
		if (::write(append_fd, &extra, 1) != 1) {
			::close(append_fd);
			throw std::runtime_error("unable to grow fixture");
		}
		::close(append_fd);

		bool rejected_growth = false;
		try {
			(void)deflateOpened(growth_snapshot);
		} catch (const std::runtime_error& error) {
			rejected_growth = std::string(error.what()).find("grew while being read") != std::string::npos;
		}
		if (!rejected_growth) {
			throw std::runtime_error("growth beyond the validated descriptor length was not rejected");
		}

		std::cout << "[PASS] compression stays bound to the validated descriptor\n";
		std::cout << "[PASS] growth beyond the validated length is rejected\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
