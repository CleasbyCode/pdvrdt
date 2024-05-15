void displayInfo() {

	std::cout << R"(
PNG Data Vehicle (pdvin v1.0.1). 
		
A steganography-like CLI tool for embedding & hiding any file type within a PNG image. 
Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

Post your file-embedded image on the following compatible sites.  

Flickr (200MB), ImgBB (32MB), PostImage (24MB), *Reddit (19MB / -r option), 
*Mastodon (16MB / -m option), *ImgPile (8MB), *X/Twitter (5MB + Dimension limits).

$ user1@linuxbox:~/Downloads/pdvrdt-main/src/pdvin$ g++ main.cpp -O2 -lz -s -o pdvin
$ user1@linuxbox:~/Downloads/pdvrdt-main/src/pdvin$ sudo cp pdvin /usr/bin
		
$ user1@linuxbox:~/Desktop$ pdvin cover_image.png hidden_image.jpg
		
To post file-embedded PNG images on Reddit, you need to use the -r option.
To post file-embedded PNG images on Mastodon, you need to use the -m opion.	
		
 -m = Mastodon option ($ pdvin -m cover_image.png file.mp3).
 -r = Reddit option ($ pdvin -r cover_image.png file.pdf). 
		
*Reddit - Post images via the new.reddit.com desktop/browser site only.
Use the Reddit "Images & Video" tab/box to upload images.

*X/Twitter - As well as the 5MB PNG image size limit, X/Twitter also has dimension size limits.
PNG-32/24 (Truecolor) 900x900 Max. 68x68 Min. 
PNG-8 (Indexed-color) 4096x4096 Max. 68x68 Min.

*ImgPile - You must sign in to an account before sharing your file-embedded PNG image on this platform.
Sharing your image without logging in, your embedded file will not be preserved.

)";
}
