//	PNG Data Vehicle (pdvin v3.3). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023
// 
//	Compile program (Linux)

//	$ sudo apt-get install libsodium-dev
//	$ g++ main.cpp -O2 -lz -lsodium -s -o pdvin
//      $ sudo cp pdvin /usr/bin

//	Run it
//	$ pdvin

#include "pdvin.h"

int main(int argc, char** argv) {
	try {
		ProgramArgs args = ProgramArgs::parse(argc, argv);
		if (!hasValidFilename(args.image_file) || !hasValidFilename(args.data_file)) {
            		throw std::runtime_error("Invalid Input Error: Unsupported characters in filename arguments.");
        	}
        	validateFiles(args.image_file, args.data_file, args.platform);
        	std::filesystem::path data_path(args.data_file);

        	bool isCompressed = isCompressedFile(data_path.extension().string());
        	pdvIn(args.image_file, args.data_file, args.platform, isCompressed);
    	}
	catch (const std::runtime_error& e) {
        	std::cerr << "\n" << e.what() << "\n\n";
        	return 1;
    	}
}
