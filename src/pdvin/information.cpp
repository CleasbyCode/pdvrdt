void displayInfo() {
	std::cout << R"(

PNG Data Vehicle (pdvin v3.4). 
Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

A steganography-like CLI tool to embed & hide any file type within a PNG image. 

Compile & run pdvin (Linux):
		
$ sudo apt-get install libsodium-dev
$ g++ main.cpp -O2 -lz -lsodium -s -o pdvin
$ sudo cp pdvin /usr/bin
$ pdvin

Usage: pdvin [-m] [-r] <cover_image> <secret_file>  
       pdvin --info
	
Post your file-embedded image on the following compatible sites.  
*Image size limits(cover image + data file):

Flickr (200MB), ImgBB (32MB), PostImage (32MB), *Reddit (19MB / -r option), 
*Mastodon (16MB / -m option), *ImgPile (8MB), *X/Twitter (5MB / *Dimension limits).

Argument options:	
		
* To create "file-embedded" PNG images compatible for posting on Mastodon, use the -m option with pdvin.
* To create "file-embedded" PNG images compatible for posting on Reddit, use the -r option with pdvin.
		
 -m = Mastodon option, (pdvin -m cover_image.png my_hidden_file.mp3).
 -r = Reddit option,   (pdvin -r cover_image.png my_hidden_file.doc).
		
From the Reddit site, click "Create Post" then always select "Images & Video" tab, to post your PNG image.

To correctly download images from X/Twitter or Reddit, click the image in the post to fully expand it, before saving.

*X/Twitter - As well as the 5MB PNG image size limit, X/Twitter also has dimension size limits.
PNG-32/24 (Truecolor) 68x68 Min. - 900x900 Max. 
PNG-8 (Indexed color) 68x68 Min. - 4096x4096 Max.

*ImgPile - You must sign in to an account before sharing your file-embedded PNG image on this platform.
Sharing your image without logging in, your embedded file will not be preserved.

)";
}
