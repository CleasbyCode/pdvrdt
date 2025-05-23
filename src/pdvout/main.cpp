//	PNG Data Vehicle (pdvout v3.4). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023
// 
//	Compile program (Linux):

//	$ sudo apt-get install libsodium-dev

//	$ chmod +x compile_pdvout.sh
//	$ ./compile_pdvout.sh

//	$ Compilation successful. Executable 'pdvout' created.
//	$ sudo cp pdvout /usr/bin
//	$ pdvout

#include "pdvOut.h"
#include "programArgs.h"
#include "fileChecks.h"

#include <iostream>         
#include <stdexcept>   

int main(int argc, char** argv) {
    try {
        ProgramArgs args = ProgramArgs::parse(argc, argv);
        if (!hasValidFilename(args.image_file)) {
            throw std::runtime_error("Invalid Input Error: Unsupported characters in filename arguments.");
        }
        validateFiles(args.image_file);
       
        pdvOut(args.image_file);
    }
    catch (const std::runtime_error& e) {
        std::cerr << "\n" << e.what() << "\n\n";
        return 1;
    }
}
