//	PNG Data Vehicle (pdvin v1.0.1). 
//	A steganography-like CLI tool for hiding files within PNG images. Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023
// 
//	Compile program (Linux)
//	$ g++ main.cpp -O2 -lz -s -o pdvin
// 
//	Run it
//	$ ./pdvin

#include "pdvin.h"

int main(int argc, char** argv) {

	bool
		isMastodonOption = false,
		isRedditOption = false;

	if (argc == 2 && std::string(argv[1]) == "--info") {
		displayInfo();
	}
	
	else if (argc > 2 && argc <= 4) {		
		if (argc == 4 && (std::string(argv[1]) != "-r" && std::string(argv[1]) != "-m")) {
			std::cerr << "\nInput Error: Invalid arguments: -r or -m only.\n\n";
			std::exit(EXIT_FAILURE);
		}
		else {
			isMastodonOption = (argc == 4 && std::string(argv[1]) == "-m") ? true : false;
			isRedditOption = (argc == 4 && !isMastodonOption) ? true : false;

			std::string 
				image_file_name = isMastodonOption || isRedditOption ? argv[2] : argv[1],
				data_file_name = isMastodonOption || isRedditOption ? argv[3] : argv[2];

			const std::regex REG_EXP("(\\.[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+)?[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+?(\\.[a-zA-Z0-9]+)?");

			const std::string GET_IMAGE_FILE_EXTENSION = image_file_name.length() > 2 ? image_file_name.substr(image_file_name.length() - 3) : image_file_name;
			
			if (GET_IMAGE_FILE_EXTENSION != "png" || !regex_match(image_file_name, REG_EXP) || !regex_match(data_file_name, REG_EXP)) {
				std::cerr << (GET_IMAGE_FILE_EXTENSION != "png" ?
					"\nFile Type Error: Invalid file extension found. Expecting only '.png'"
					: "\nInvalid Input Error: Characters not supported by this program found within file name arguments") << ".\n\n";
				std::exit(EXIT_FAILURE);
			}

			startPdv(image_file_name, data_file_name, isMastodonOption, isRedditOption);
		}
	}
	else {
		std::cout << "\nUsage:\tpdvin [-m] [-r] <cover_image> <data_file>\n\tpdvin --info\n\n";
	}
	return 0;
}
