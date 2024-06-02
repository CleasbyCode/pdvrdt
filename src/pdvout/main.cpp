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
			IMAGE_FILENAME = std::string(argv[1]),
			IMAGE_FILE_EXTENSION = IMAGE_FILENAME.length() > 2 ? IMAGE_FILENAME.substr(IMAGE_FILENAME.length() - 3) : IMAGE_FILENAME;

		if (IMAGE_FILE_EXTENSION == "png" && regex_match(IMAGE_FILENAME, REG_EXP) && std::filesystem::exists(IMAGE_FILENAME)) {
			startPdv(IMAGE_FILENAME);
		} else {
			std::cerr << (IMAGE_FILE_EXTENSION != "png" 
				? "\nFile Type Error: Invalid file extension. Expecting only \"png\""
		   		: !regex_match(IMAGE_FILENAME, REG_EXP) 
					? "\nInvalid Input Error: Characters not supported by this program found within filename arguments"
						: "\nImage File Error: File not found. Check the filename and try again")
			<< ".\n\n";
			
		  	std::exit(EXIT_FAILURE);
		}
	}
	return 0;
}
