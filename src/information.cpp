#include "information.h"
#include <iostream>

void displayInfo() {
	std::cout << R"(
 PNG Data Vehicle (pdvrdt v3.6)
 Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

 pdvrdt is a "steganography-like" command-line tool used for concealing and extracting any file type via a PNG image. 

 Compile & run pdvrdt (Linux):
		
  $ sudo apt-get install libsodium-dev

  $ chmod +x compile_pdvrdt.sh
  $ ./compile_pdvrdt.sh

  $ Compilation successful. Executable 'pdvrdt' created.

  $ sudo cp pdvrdt /usr/bin
  $ pdvrdt
		
  Usage: pdvrdt conceal [-m|-r] <cover_image> <secret_file>
  	 pdvrdt recover <cover_image> 
         pdvrdt --info
		
 Share your "file-embedded" PNG image on the following compatible sites.
 
 Size limit for these platforms measured by the combined size of cover image & compressed data file.
 
  Flickr (200MB), ImgBB (32MB), PostImage (32MB), Reddit (19MB / -r option),
  Mastodon (16MB / -m option), ImgPile (8MB), X-Twitter (5MB + *Dimension size limits).
  
  X-Twitter Image Dimension Size Limits:
  
   PNG-32/24 (Truecolor) 68x68 Min. --- 900x900 Max.
   PNG-8 (Indexed-color) 68x68 Min. --- 4096x4096 Max.	
 
 pdvrdt mode arguments:
 
  conceal - Compresses, encrypts and embeds your secret data file within a PNG cover image.
  recover - Decrypts, uncompresses and extracts the concealed data file from a PNG cover image (recovery PIN required!).
 
 pdvrdt conceal mode platform options:
 
  -m (Mastodon). To share/post compatible "file-embedded" PNG images on Mastodon, you must use the -m option with conceal mode.
 
  $ pdvrdt conceal -m my_image.png hidden.doc
 
  -r (Reddit). To share/post compatible "file-embedded" PNG images on Reddit, you must use the -r option with conceal mode.
	
  $ pdvrdt conceal -r my_image.png secret.mp3 

  From the Reddit site, click "Create Post" then select the "Images & Video" tab, to attach and post your PNG image.
  
 To correctly download images from X-Twitter or Reddit, click the image in the post to fully expand it, before saving.
		
)";
}
