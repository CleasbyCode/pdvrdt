#pragma once

#include "common.h"

void displayInfo();

struct ProgramArgs {
	Mode mode{Mode::conceal};
	Option option{Option::None};
	fs::path image_file_path{};
	fs::path data_file_path{};

	[[noreturn]] static void die(const std::string& usage);
	static std::optional<ProgramArgs> parse(int argc, char** argv);
};
