#include "information.h"
#include <iostream>

void displayInfo() {
	std::cout << R"(

PNG Data Vehicle (pdvout v3.5). 
Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

A steganography-like CLI tool to extract hidden data from a (pdvin) PNG image. 

Compile & run pdvout (Linux):
		
$ sudo apt-get install libsodium-dev
$
$ chmod +x compile_pdvout.sh
$ ./compile_pdvout.sh
$
$ Compilation successful. Executable 'pdvout' created.
$ sudo cp pdvout /usr/bin
$ pdvout

Usage: pdvout <file-embedded-image>
       pdvout --info

)";
}
