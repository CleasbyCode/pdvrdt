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
	} else if (argc > 2 && argc < 5) {
		bool
			isFileCheckSuccess = false,
			isMastodonOption = (argc > 3 && std::string(argv[1]) == "-m") ? true : false,
			isRedditOption = (argc > 3 && !isMastodonOption) ? true : false;

		constexpr const char* REG_EXP = ("(\\.[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+)?[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+?(\\.[a-zA-Z0-9]+)?");
		const std::regex regex_pattern(REG_EXP);

		const std::string IMAGE_FILENAME = isMastodonOption || isRedditOption ? argv[2] : argv[1];

		std::string data_filename = isMastodonOption || isRedditOption ? argv[3] : argv[2];

		std::filesystem::path image_path(IMAGE_FILENAME);
		std::string image_extension = image_path.extension().string();

		if (image_extension != ".png" || (argc > 3 && std::string(argv[1]) != "-r" && std::string(argv[1]) != "-m")) {
			std::cerr << (image_extension != ".png" 
				? "\nFile Type Error: Invalid file extension. Expecting only \"png\""
				: "\nInput Error: Invalid arguments. Expecting only -m or -r") 
			<< ".\n\n";
		} else if (!regex_match(IMAGE_FILENAME, regex_pattern) || !regex_match(data_filename, regex_pattern)) {
			std::cerr << "\nInvalid Input Error: Characters not supported by this program found within filename arguments.\n\n";
		} else if (!std::filesystem::exists(IMAGE_FILENAME) || !std::filesystem::exists(data_filename) || !std::filesystem::is_regular_file(data_filename)) {
			std::cerr << (!std::filesystem::exists(IMAGE_FILENAME) 
				? "\nImage"
				: "\nData") 
			<< " File Error: File not found. Check the filename and try again.\n\n";
		} else {
			isFileCheckSuccess = true;
		}
		if (isFileCheckSuccess) {
			pdvIn(IMAGE_FILENAME, data_filename, isMastodonOption, isRedditOption);			
		} else {
		  	return 1;
		}   
	} else {
	    std::cout << "\nUsage: pdvin [-m|-r] <cover_image> <data_file>\n\t\bpdvin --info\n\n";
	}
}
