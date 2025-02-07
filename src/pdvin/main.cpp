//	PNG Data Vehicle (pdvin v3.2). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023
// 
//	Compile program (Linux)
//	$ sudo apt-get install libsodium-dev
//	$ g++ main.cpp -O2 -lz -lsodium -s -o pdvin
//      $ sudo cp pdvin /usr/bin

//	Run it
//	$ pdvin

enum class ArgOption {
	Default,
	Mastodon,
	Reddit
};

#include "pdvin.h"

int main(int argc, char** argv) {
	if (argc == 2 && std::string(argv[1]) == "--info") {
		displayInfo();
		return 0;
	}

	if (argc < 3 || argc > 4) {
		std::cout << "\nUsage: pdvin [-m|-r] <cover_image> <data_file>\n\t\bpdvin --info\n\n";
		return 1;
	}
	
	ArgOption platformOption = ArgOption::Default;
    	uint8_t argIndex = 1;

	if (argc == 4) {
		if (std::string(argv[1]) != "-r" && std::string(argv[1]) != "-m") {
         		std::cerr << "\nInput Error: Invalid arguments. Only expecting \"-r or -m\" as the optional arguments.\n\n";
         		return 1;
    	 	}
     	 platformOption = std::string(argv[1]) == "-r" ? ArgOption::Reddit : ArgOption::Mastodon;
     	 argIndex = 2;
    	}

        const std::string IMAGE_FILENAME = argv[argIndex];
        std::string data_filename        = argv[++argIndex];

	constexpr const char* REG_EXP = ("(\\.[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+)?[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+?(\\.[a-zA-Z0-9]+)?");
	const std::regex regex_pattern(REG_EXP);

	if (!regex_match(IMAGE_FILENAME, regex_pattern) || !regex_match(data_filename, regex_pattern)) {
		std::cerr << "\nInvalid Input Error: Characters not supported by this program found within filename arguments.\n\n";
		return 1;
	}

	const std::filesystem::path 
		IMAGE_PATH(IMAGE_FILENAME),
		DATA_FILE_PATH(data_filename);

   	const std::string 
		IMAGE_EXTENSION = IMAGE_PATH.extension().string(),
		DATA_FILE_EXTENSION = DATA_FILE_PATH.extension().string();

	if (IMAGE_EXTENSION != ".png") {
		std::cerr << "\nFile Type Error: Invalid file extension. Only expecting \"png\" image extension.\n\n";
		return 1;
	}

	if (!std::filesystem::exists(IMAGE_FILENAME) || !std::filesystem::exists(data_filename) || !std::filesystem::is_regular_file(data_filename)) {
		std::cerr << (!std::filesystem::exists(IMAGE_FILENAME)
			? "\nImage File Error: File not found."
			: "\nData File Error: File not found or not a regular file.")
			<< " Check the filename and try again.\n\n";
		return 1;
	}
	const std::set<std::string> COMPRESSED_FILE_EXTENSIONS = { ".zip", "jar", ".rar", ".7z", ".bz2", ".gz", ".xz", ".tar", ".lz", ".lz4", ".cab", ".rpm", ".deb", ".mp4", ".mp3", ".jpg", ".png", ".ogg", ".flac" };

    	const bool isCompressedFile = COMPRESSED_FILE_EXTENSIONS.count(DATA_FILE_EXTENSION) > 0;

	pdvIn(IMAGE_FILENAME, data_filename, platformOption, isCompressedFile);			
}
