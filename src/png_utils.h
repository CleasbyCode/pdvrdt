#pragma once

#include "common.h"

[[nodiscard]] std::optional<std::size_t> searchSig(
	std::span<const Byte> data,
	std::span<const Byte> sig,
	std::size_t start = 0);

template <std::size_t Length = 4>
	requires (Length == 4 || Length == 8)
void updateValue(std::span<Byte> data, std::size_t index, auto value) {
	using ValueType = std::conditional_t<Length == 4, uint32_t, uint64_t>;

	if (index + Length > data.size()) {
		throw std::out_of_range("updateValue: Index out of bounds.");
	}

	ValueType val = std::byteswap(static_cast<ValueType>(value));

	std::memcpy(data.data() + index, &val, Length);
}

template <std::size_t Length = 4>
	requires (Length == 4 || Length == 8)
[[nodiscard]] std::size_t getValue(std::span<const Byte> data, std::size_t index) {
	using ValueType = std::conditional_t<Length == 4, uint32_t, uint64_t>;

	if (index + Length > data.size()) {
		throw std::out_of_range("getValue: Index out of bounds.");
	}

	ValueType val;
	std::memcpy(&val, data.data() + index, Length);

	return static_cast<std::size_t>(std::byteswap(val));
}
