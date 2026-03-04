#pragma once

#include "common.h"

void zlibDeflate(vBytes& data_vec, Option option, bool is_compressed_file = false);
void zlibInflate(vBytes& data_vec);
[[nodiscard]] vBytes zlibInflateSpanBounded(std::span<const Byte> data, std::size_t max_output_size);
[[nodiscard]] std::size_t zlibInflateToFd(const vBytes& data_vec, int fd);
