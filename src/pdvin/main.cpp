//	PNG Data Vehicle (pdvin v1.0.4). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023
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

		const std::regex REG_EXP("(\\.[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+)?[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+?(\\.[a-zA-Z0-9]+)?");

		const std::string
			IMAGE_FILENAME = isMastodonOption || isRedditOption ? argv[2] : argv[1],
			IMAGE_FILE_EXTENSION = IMAGE_FILENAME.length() > 2 ? IMAGE_FILENAME.substr(IMAGE_FILENAME.length() - 3) : IMAGE_FILENAME;

		std::string data_filename = isMastodonOption || isRedditOption ? argv[3] : argv[2];

		if (IMAGE_FILE_EXTENSION != "png" || (argc > 3 && std::string(argv[1]) != "-r" && std::string(argv[1]) != "-m")) {
			std::cerr << (IMAGE_FILE_EXTENSION != "png" 
				? "\nFile Type Error: Invalid file extension. Expecting only \"png\""
				: "\nInput Error: Invalid arguments. Expecting only -m or -r") 
			<< ".\n\n";
		} else if (!regex_match(IMAGE_FILENAME, REG_EXP) || !regex_match(data_filename, REG_EXP)) {
			std::cerr << "\nInvalid Input Error: Characters not supported by this program found within filename arguments.\n\n";
		} else if (!std::filesystem::exists(IMAGE_FILENAME) || !std::filesystem::exists(data_filename) || !std::filesystem::is_regular_file(data_filename)) {
			std::cerr << (!std::filesystem::exists(IMAGE_FILENAME) 
				? "\nImage"
				: "\nData") 
			<< " File Error: File not found. Check the filename and try again.\n\n";
		} else {
			isFileCheckSuccess = true;
		}
		isFileCheckSuccess ? startPdv(IMAGE_FILENAME, data_filename, isMastodonOption, isRedditOption) : std::exit(EXIT_FAILURE);	   
	} else {
	    std::cout << "\nUsage: pdvin [-m|-r] <cover_image> <data_file>\n\t\bpdvin --info\n\n";
	}
}
