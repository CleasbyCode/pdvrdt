//	PNG Data Vehicle (pdvin v1.0.3). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023
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
		if (argc > 3 && (std::string(argv[1]) != "-r" && std::string(argv[1]) != "-m")) {
			std::cerr << "\nInput Error: Invalid arguments: -r or -m only.\n\n";
			std::exit(EXIT_FAILURE);
		} else {
			bool
				isFileCheckSuccess = false,
				isMastodonOption = (argc > 3 && std::string(argv[1]) == "-m") ? true : false,
				isRedditOption = (argc > 3 && !isMastodonOption) ? true : false;

			const std::regex REG_EXP("(\\.[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+)?[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+?(\\.[a-zA-Z0-9]+)?");

			const std::string
				IMAGE_FILE_NAME = isMastodonOption || isRedditOption ? argv[2] : argv[1],
				IMAGE_FILE_EXTENSION = IMAGE_FILE_NAME.length() > 2 ? IMAGE_FILE_NAME.substr(IMAGE_FILE_NAME.length() - 3) : IMAGE_FILE_NAME;

			std::string data_file_name = isMastodonOption || isRedditOption ? argv[3] : argv[2];

			if (IMAGE_FILE_EXTENSION != "png") {
				std::cerr << "\nFile Type Error: Invalid file extension. Expecting only '.png'\n\n";
			} else if (!regex_match(IMAGE_FILE_NAME, REG_EXP) || !regex_match(data_file_name, REG_EXP)) {
				std::cerr << "\nInvalid Input Error: Characters not supported by this program found within file name arguments.\n\n";
			} else if (!std::filesystem::exists(IMAGE_FILE_NAME) || !std::filesystem::exists(data_file_name)) {
				std::cerr << "\nFile Error: File not found. Check your "
					<< (!std::filesystem::exists(IMAGE_FILE_NAME) ? "image filename " : "data file filename ")  << "and try again.\n\n";
			} else {
				isFileCheckSuccess = true;
			}
			isFileCheckSuccess ? startPdv(IMAGE_FILE_NAME, data_file_name, isMastodonOption, isRedditOption) : std::exit(EXIT_FAILURE);
		}	   
	} else {
	    std::cout << "\nUsage: pdvin [-m] [-r] <cover_image> <data_file>\n\t\bpdvin --info\n\n";
	}
	return 0;
}
