#pragma once

#include <string>

enum class ArgOption {
	Default,
	Mastodon,
	Reddit
};

// Struct for parsing program arguments
struct ProgramArgs {
	ArgOption platform = ArgOption::Default;
    	std::string image_file;
    	std::string data_file;

    	static ProgramArgs parse(int argc, char** argv);
};

