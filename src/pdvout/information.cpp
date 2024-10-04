void displayInfo() {
	std::cout << R"(

PNG Data Vehicle (pdvout v1.2). 
Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

A steganography-like CLI tool to extract hidden data from a (pdvin) PNG image. 

Compile & run pdvout (Linux):
		
$ g++ main.cpp -O2 -lz -s -o pdvout
$ sudo cp pdvout /usr/bin
$ pdvout

Usage: pdvout <file-embedded-image>
       pdvout --info

)";
}
