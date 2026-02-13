#include "png_utils.h"

std::optional<std::size_t> searchSig(
	std::span<const Byte> data,
	std::span<const Byte> sig,
	std::size_t start) {

	if (start >= data.size()) {
		return std::nullopt;
	}

	const auto search_range = data.subspan(start);
	const auto result = std::ranges::search(search_range, sig);

	if (result.empty()) {
		return std::nullopt;
	}

	return start + static_cast<std::size_t>(std::ranges::distance(search_range.begin(), result.begin()));
}
