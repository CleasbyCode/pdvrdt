//	PNG Data Vehicle (pdvout v1.0.1). 
//	A steganography-like CLI tool for extracting (pdvin) hidden files from PNG images. Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023
// 
//	Compile program (Linux)
//	$ g++ main.cpp -O2 -lz -s -o pdvout
// 
//	Run it
//	$ ./pdvout

#include "pdvout.h"

int main(int argc, char** argv) {

	if (argc == 2 && std::string(argv[1]) == "--info") {
		displayInfo();

	} else if (argc == 2) {

		std::string image_file_name = std::string(argv[1]);

		const std::regex REG_EXP("(\\.[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+)?[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+?(\\.[a-zA-Z0-9]+)?");

		const std::string IMAGE_FILE_EXTENSION = image_file_name.length() > 2 ? image_file_name.substr(image_file_name.length() - 3) : image_file_name;

		if (IMAGE_FILE_EXTENSION != "png" || !regex_match(image_file_name, REG_EXP)) {
			std::cerr << (IMAGE_FILE_EXTENSION != "png" ?
				"\nFile Type Error: Invalid file extension found. Expecting only '.png'"
				: "\nInvalid Input Error: Characters not supported by this program found within file name arguments") << ".\n\n";
			std::exit(EXIT_FAILURE);
		}

		startPdv(image_file_name);

	} else {
		std::cout << "\nUsage:\tpdvout <file_embedded_image>\n\tpdvout --info\n\n";
	}
	return 0;
}