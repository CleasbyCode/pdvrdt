//	PNG Data Vehicle (pdvout v2.1). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023
// 
//	Compile program (Linux)
//	$ g++ main.cpp -O2 -lz -s -o pdvout
// 	$ sudo cp pdvout /usr/bin

//	Run it
//	$ pdvout

#include "pdvout.h"

int main(int argc, char** argv) {
	if (argc != 2) {
		std::cout << "\nUsage: pdvout <file_embedded_image>\n\t\bpdvout --info\n\n";
		return 1;
	}

	if (std::string(argv[1]) == "--info") {
		displayInfo();
		return 0;
	}

	const std::string IMAGE_FILENAME = std::string(argv[1]);

	constexpr const char* REG_EXP = ("(\\.[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+)?[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+?(\\.[a-zA-Z0-9]+)?");
	const std::regex regex_pattern(REG_EXP);

	if (!std::regex_match(IMAGE_FILENAME, regex_pattern)) {
		std::cerr << "\nInvalid Input Error: Characters not supported by this program found within filename arguments.\n\n";
        return 1;
	}

	std::filesystem::path image_path(IMAGE_FILENAME);
	const std::string IMAGE_EXTENSION = image_path.extension().string();

	if (IMAGE_EXTENSION != ".png") {
		std::cerr << "\nFile Type Error: Invalid file extension. Expecting only \"png\" image extension.\n\n";
        return 1;	
	}
	
	if (!std::filesystem::exists(IMAGE_FILENAME)) {
		std::cerr << "\nImage File Error: File not found. Check the filename and try again.\n\n";
        return 1;
	}
	pdvOut(IMAGE_FILENAME);
}
