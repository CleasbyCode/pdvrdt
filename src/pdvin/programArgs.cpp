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

ProgramArgs ProgramArgs::parse(int argc, char** argv) {
	ProgramArgs args;
	if (argc == 2 && std::string(argv[1]) == "--info") {
		displayInfo();
        	std::exit(0);
	}

	if (argc < 3 || argc > 4) {
        	throw std::runtime_error("Usage: pdvin [-m|-r] <cover_image> <secret_file>\n\t\bpdvin --info");
    	}

    	uint8_t arg_index = 1;

    	if (argc == 4) {
		if (std::string(argv[arg_index]) != "-r" && std::string(argv[arg_index]) != "-m") {
            		throw std::runtime_error("Input Error: Invalid arguments. Only expecting \"-r or -m\" as the optional arguments");
        	}
	args.platform = std::string(argv[arg_index]) == "-r" ? ArgOption::Reddit : ArgOption::Mastodon;
        arg_index = 2;
    	}

    	args.image_file = argv[arg_index];
    	args.data_file = argv[++arg_index];
    	return args;
}
