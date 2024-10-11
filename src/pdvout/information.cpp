void displayInfo() {
	std::cout << R"(

PNG Data Vehicle (pdvout v1.3). 
Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

pdvout is a steganography-like CLI tool for extracting hidden data from a (pdvin) "file-embedded" PNG image. 

Compile & run pdvout (Linux):
		
$ g++ main.cpp -O2 -lz -s -o pdvout
$ sudo cp pdvout /usr/bin
$ pdvout

Usage: pdvout <file-embedded-image>
       pdvout --info

)";
}
