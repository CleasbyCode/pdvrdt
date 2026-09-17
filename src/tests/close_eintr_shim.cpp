#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace {
using CloseFunction = int (*)(int);
std::atomic<bool> injected{false};
}

extern "C" int close(int fd) {
	static const auto real_close = reinterpret_cast<CloseFunction>(dlsym(RTLD_NEXT, "close"));
	if (real_close == nullptr) {
		errno = EIO;
		return -1;
	}

	char proc_path[64]{};
	std::snprintf(proc_path, sizeof(proc_path), "/proc/self/fd/%d", fd);
	char target[4096]{};
	const ssize_t target_len = ::readlink(proc_path, target, sizeof(target) - 1);
	const bool is_pdvrdt_output =
		target_len > 0 && std::strstr(target, "/prdt_") != nullptr;

	const int result = real_close(fd);
	if (is_pdvrdt_output && !injected.exchange(true)) {
		errno = EINTR;
		return -1;
	}
	return result;
}
