void displayInfo() {
	std::cout << R"(

PNG Data Vehicle (pdvin v1.2). 
Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

pdvin is a steganography-like CLI tool for embedding & concealing any file type within a PNG image.

Compile & run pdvin (Linux):
		
$ g++ main.cpp -O2 -lz -s -o pdvin
$ sudo cp pdvin /usr/bin
$ pdvin

Usage: pdvin [-m] [-r] <cover_image> <data_file>  
       pdvin --info
	
Post your file-embedded image on the following compatible sites.  
Image size limits(cover image + data file):

Flickr (200MB), ImgBB (32MB), PostImage (24MB), *Reddit (19MB / -r option), 
Mastodon (16MB / -m option), ImgPile (8MB), X/Twitter (5MB / Dimension limits).

Argument options:	
		
To post/share file-embedded PNG images on Reddit use the -r option with pdvin.
To post/share file-embedded PNG images on Mastodon use the -m option with pdvin.
		
 -m = Mastodon option, (pdvin -m cover_image.png my_hidden_file.mp3).
 -r = Reddit option,   (pdvin -r cover_image.png my_hidden_file.doc).
		
From the Reddit site, click "Create Post" then select "Images & Video" tab, to post your PNG image.
		
X/Twitter - As well as the 5MB PNG image size limit, X/Twitter also has dimension size limits.

PNG-32/24 (Truecolor) 900x900 Max. 68x68 Min. 
PNG-8 (Indexed color) 4096x4096 Max. 68x68 Min.

ImgPile - You must sign in to an account before sharing your file-embedded PNG image on this platform.
Sharing your image without logging in, your embedded file will not be preserved.

)";
}
