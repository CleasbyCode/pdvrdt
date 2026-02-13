#pragma once

#include "common.h"

void zlibDeflate(vBytes& data_vec, Option option, bool is_compressed_file = false);
void zlibInflate(vBytes& data_vec);
