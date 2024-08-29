//	PNG Data Vehicle (pdvout v1.1.8). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023
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
		constexpr const char* REG_EXP = ("(\\.[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+)?[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+?(\\.[a-zA-Z0-9]+)?");
		const std::regex regex_pattern(REG_EXP);

		const std::string IMAGE_FILENAME = std::string(argv[1]);

		std::filesystem::path image_path(IMAGE_FILENAME);
		std::string image_extension = image_path.extension().string();

		if (image_extension == ".png" && regex_match(IMAGE_FILENAME, regex_pattern) && std::filesystem::exists(IMAGE_FILENAME)) {
			pdvOut(IMAGE_FILENAME);
		} else {
			std::cerr << (image_extension != ".png" 
				? "\nFile Type Error: Invalid file extension. Expecting only \"png\""
		   		: !regex_match(IMAGE_FILENAME, regex_pattern) 
					? "\nInvalid Input Error: Characters not supported by this program found within filename arguments"
						: "\nImage File Error: File not found. Check the filename and try again")
			<< ".\n\n";
			return 1;
		}
	}
}
