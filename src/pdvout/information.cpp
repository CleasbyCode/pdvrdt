void displayInfo() {

	std::cout << R"(
PNG Data Vehicle (pdvout v1.0.1). 
A steganography-like CLI tool for extracting (pdvin) hidden files from PNG images. 
Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.
		
$ user1@linuxbox:~/Downloads/pdvrdt-main/src/pdvout$ g++ main.cpp -O2 -lz -s -o pdvout
$ user1@linuxbox:~/Downloads/pdvrdt-main/src/pdvout$ sudo cp pdvout /usr/bin
		
$ user1@linuxbox:~/Desktop$ pdvout file_embedded_image.png

)";
}
