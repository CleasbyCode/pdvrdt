//	PNG Data Vehicle (pdvout v1.0.3). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023
// 
//	Compile program (Linux)
//	$ g++ main.cpp -O2 -lz -s -o pdvout
// 
//	Run it
//	$ ./pdvout

#include "pdvout.h"

int main(int argc, char** argv) {
	if (argc !=2) {
		std::cout << "\nUsage: pdvout <file_embedded_image>\n\t\bpdvout --info\n\n";
	} else if (std::string(argv[1]) == "--info") {
		displayInfo();
	} else {
		const std::regex REG_EXP("(\\.[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+)?[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+?(\\.[a-zA-Z0-9]+)?");

		const std::string 
			IMAGE_FILE_NAME = std::string(argv[1]),
			IMAGE_FILE_EXTENSION = IMAGE_FILE_NAME.length() > 2 ? IMAGE_FILE_NAME.substr(IMAGE_FILE_NAME.length() - 3) : IMAGE_FILE_NAME;

		if (IMAGE_FILE_EXTENSION == "png" && regex_match(IMAGE_FILE_NAME, REG_EXP) && std::filesystem::exists(IMAGE_FILE_NAME)) {
			startPdv(IMAGE_FILE_NAME);
		} else {
			std::cerr << (IMAGE_FILE_EXTENSION != "png" 
				? "\nFile Type Error: Invalid file extension. Expecting only \"png\".\n\n"
		   		: !regex_match(IMAGE_FILE_NAME, REG_EXP) 
					? "\nInvalid Input Error: Characters not supported by this program found within filename arguments.n\n"
						: "\nImage File Error: File not found. Check the filename and try again.\n\n");
		  	std::exit(EXIT_FAILURE);
		}
	}
	return 0;
}
