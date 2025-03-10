void displayInfo() {
	std::cout << R"(

PNG Data Vehicle (pdvout v3.3). 
Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

A steganography-like CLI tool to extract hidden data from a (pdvin) PNG image. 

Compile & run pdvout (Linux):
		
$ sudo apt-get install libsodium-dev
$ g++ main.cpp -O2 -lz -lsodium -s -o pdvout
$ sudo cp pdvout /usr/bin
$ pdvout

Usage: pdvout <file-embedded-image>
       pdvout --info

)";
}