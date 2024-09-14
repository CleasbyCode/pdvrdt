//	PNG Data Vehicle (pdvin v1.1.8). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023
// 
//	Compile program (Linux)
//	$ g++ main.cpp -O2 -lz -s -o pdvin
// 
//	Run it
//	$ ./pdvin

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
	
	bool
		isMastodonOption = (argc > 3 && std::string(argv[1]) == "-m"),
		isRedditOption = (argc > 3 && !isMastodonOption);

	const std::string IMAGE_FILENAME = isMastodonOption || isRedditOption ? argv[2] : argv[1];
	std::string data_filename = isMastodonOption || isRedditOption ? argv[3] : argv[2];

	std::filesystem::path image_path(IMAGE_FILENAME);
	std::string image_extension = image_path.extension().string();

	if (argc > 3 && std::string(argv[1]) != "-r" && std::string(argv[1]) != "-m") {
		std::cerr << "\nInput Error: Invalid arguments. Expecting only \"-m\" or \"-r\" as the first argument option.\n\n";
		return 1;
	}

	if (image_extension != ".png") {
		std::cerr << "\nFile Type Error: Invalid file extension. Expecting only \".png\" image extension.\n\n";
		return 1;
	}

	constexpr const char* REG_EXP = ("(\\.[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+)?[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+?(\\.[a-zA-Z0-9]+)?");
	const std::regex regex_pattern(REG_EXP);

	if (!regex_match(IMAGE_FILENAME, regex_pattern) || !regex_match(data_filename, regex_pattern)) {
		std::cerr << "\nInvalid Input Error: Characters not supported by this program found within filename arguments.\n\n";
		return 1;
	}
	
	if (!std::filesystem::exists(IMAGE_FILENAME) || !std::filesystem::exists(data_filename) || !std::filesystem::is_regular_file(data_filename)) {
		std::cerr << (!std::filesystem::exists(IMAGE_FILENAME)
			? "\nImage"
			: "\nData")
			<< " File Error: File not found or not a regular file. Check the filename and try again.\n\n";
		return 1;
	}
	pdvIn(IMAGE_FILENAME, data_filename, isMastodonOption, isRedditOption);			
}
