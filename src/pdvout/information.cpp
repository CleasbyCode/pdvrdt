void displayInfo() {

	std::cout << R"(
PNG Data Vehicle (pdvrdt v1.8). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

A simple command-line tool used to embed and extract any file type via a PNG image file.  
Share your file-embedded image on the following compatible sites.  

Image size limit is platform dependant:-  

  Flickr (200MB), ImgBB (32MB), PostImage (24MB), *Reddit (19MB / -r option), 
  *Mastodon (16MB / -m option), *ImgPile (8MB), *Twitter (5MB / Dimension limits).

Argument options:	

 -e = Embed File Mode, 
 -x = eXtract File Mode,
 -m = Mastodon option, used with -e Embed mode (pdvrdt -e -m image.png file.mp3).
 -r = Reddit option, used with -e Embed mode (pdvrdt -e -r image.png file.doc).

To post/share file-embedded PNG images on Reddit, you need to use the -r option.
To post/share file-embedded PNG images on Mastodon, you need to use the -m opion.
	
*Reddit - Post images via the new.reddit.com desktop / browser site only. Use the "Images & Video" tab/box.

*Twitter - As well as the 5MB PNG image size limit, Twitter also has dimension size limits.
PNG-32/24 (Truecolor) 900x900 Max. 68x68 Min. 
PNG-8 (Indexed color) 4096x4096 Max. 68x68 Min.

*ImgPile - You must sign in to an account before sharing your file-embedded PNG image on this platform.
Sharing your image without logging in, your embedded file will not be preserved.

This program works on Linux and Windows.

)";
}
