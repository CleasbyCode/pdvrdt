void displayInfo() {
	std::cout << R"(

PNG Data Vehicle (pdvin v1.3). 
Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

pdvin is a steganography-like CLI tool for embedding & concealing any file type within a PNG image.

Compile & run pdvin (Linux):
		
$ g++ main.cpp -O2 -lz -s -o pdvin
$ sudo cp pdvin /usr/bin
$ pdvin

Usage: pdvin [-m|-r] <cover_image> <data_file>  
       pdvin --info
	
Post your "file-embedded" PNG image on the following compatible sites.  
Image size limits (cover image + data file):

Flickr (200MB), ImgBB (32MB), PostImage (32MB), Reddit (19MB / -r option), 
Mastodon (16MB / -m option), ImgPile (8MB), X/Twitter (5MB / Dimension limits).

Argument options:	
		
To create "file-embedded" PNG images compatible for posting on:
Mastodon. Use the -m option (pdvin -m cover_image.png my_hidden_file.mp3).
Reddit. Use the -r option (pdvin -r cover_image.png my_hidden_file.doc).
		
From the Reddit site, click "Create Post" then select "Images & Video" tab, to post your PNG image.
		
In addition to the 5MB image size limit, X/Twitter also has the following dimension size limits.

PNG-32/24 (Truecolor) 68x68 Min. - 900x900 Max.
PNG-8 (Indexed-color) 68x68 Min. - 4096x4096 Max.
		
To correctly download images from X/Twitter or Reddit, click the image in the post to fully expand it, before saving.
		
ImgPile - You must sign in to an account before sharing your "file-embedded" PNG image on this site.
Sharing your image without logging in, your embedded data will not be preserved.

)";
}
