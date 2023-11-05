# pdvrdt

A simple command-line tool to embed and extract any file type via a PNG image.  

Share your *file-embedded* image on the following compatible sites.  

* ***Flickr (200MB), ImgBB (32MB), PostImage (24MB), \*Reddit (20MB / with -r option)***, 
* ***Mastodon (16MB), \*ImgPile (8MB), Imgur (5MB), \*Reddit (1MB / without -r option)***.

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/witch_pic.png)  
Image Credit: [ZOOT / @LittleTinRobot](https://twitter.com/LittleTinRobot/status/1689155758129336320)  
{***Image contains an embedded MP3 file (7MB) / extract: pdvrdt -x witch_pic.png)***} 

[**Video Demo 1: Mastodon**](https://youtu.be/-zFJcljHzZU)   
[**Video Demo 2: Reddit**](https://youtu.be/j7BC31nVrMg)  

**\*ImgPile** - *You must sign in to an account before sharing your data-embedded PNG image on this platform.  
Sharing your image without logging in, your embedded data will not be preserved.*  

**\*Redditt** - *Desktop / Browser support only. **Reddit** mobile app not supported as it converts  
all images to Webp format.*  

*Using the -r option when embedding a data file increases the **Reddit** upload size limit from **1MB** to **20MB**.*  
*The data-embedded PNG image created with the -r option can only be shared on Reddit and is incompatible  
with the other platforms listed above.*

***If your data file is under ***10KB*** and image dimensions 900x900 or less (PNG-32/24),  
4096x4096 or less (PNG-8), you can also share your *file-embedded* PNG image on ***Twitter***.***  

To embed larger files for ***Twitter*** (***5MB max.***), please try **[pdvzip](https://github.com/CleasbyCode/pdvzip)**  

For ***Flickr, ImgBB, PostImage, \*Reddit (with -r option), Mastodon, ImgPile & Imgur***,  
the size limit is measured by the total size of your *file-embedded* PNG image.  

For ***\*Reddit (without -r option)*** and ***Twitter***, the size limit is measured by the uncompressed size (zlib inflate)  
of the ***iCCP chunk***, where your data is stored (encrypted & compressed).  

![profile Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/iccp.png)  

***\*Reddit (without -r option)***: 1,048,172 bytes is the uncompressed (zlib inflate) size limit for your data file.  
404 bytes is used for the basic ***iCC Profile***. (404 + 1048172 = 1,048,576 bytes [***1MB***]).

***Twitter***: 9,836 bytes is the uncompressed (zlib inflate) limit for your data file.  
404 bytes is used for the basic ***iCC Profile*** (404 + 9836 = 10,240 bytes [***10KB***])

To maximise the amount of data you can embed in your image file for ***Reddit (without -r option)*** or ***Twitter***,  
first compress the data file to a ***ZIP*** or ***RAR*** file. Make sure the compressed ***ZIP*** or ***RAR***  
file does not exceed 1,048,172 bytes for ***Reddit (without -r option)*** or 9,836 bytes for ***Twitter***. 

You can insert up to ***six*** files at a time (outputs one image per file).  
You can also extract files from up to ***six*** images at a time.

Compile and run the program under Windows or **Linux**.

## Usage (Linux - Insert file into PNG image / Extract file from image)

```console

user1@linuxbox:~/Desktop$ g++ pdvrdt.cpp -O2 -lz -s -o pdvrdt
user1@linuxbox:~/Desktop$
user1@linuxbox:~/Desktop$ ./pdvrdt 

Usage:  pdvrdt -i [-r] <png_image> <file(s)>  
	pdvrdt -x <png_image(s)>  
	pdvrdt --info

user1@linuxbox:~/Desktop$ ./pdvrdt -i rabbit.png document.pdf
  
Insert mode selected.

Reading files. Please wait...

Encrypting data file.

Compressing data file.

Embedding data file within the iCCP chunk of the PNG image.

Writing data-embedded PNG image out to disk.

Created data-embedded PNG image: "pdv_img1.png" Size: "1245285 Bytes".

Complete!

You can now post your data-embedded PNG image(s) to the relevant supported platforms.

user1@linuxbox:~/Desktop$ ./pdvrdt

Usage:  pdvrdt -i [-r] <png_image> <file(s)>  
	pdvrdt -x <png_image(s)>  
	pdvrdt --info
        
user1@linuxbox:~/Desktop$ ./pdvrdt -x pdv_img1.png

Extract mode selected.

Reading embedded PNG image file. Please wait...

Found compressed iCCP chunk.

Inflating iCCP chunk.

Found pdvrdt embedded data file.

Extracting encrypted data file from the iCCP chunk.

Decrypting extracted data file.

Writing decrypted data file out to disk.

Saved file: "document.pdf" Size: "1016540 Bytes"

Complete! Please check your extracted file(s).
  
user1@linuxbox:~/Desktop$ 

```

 My other programs you may find useful:-
 
* [pdvzip: CLI tool to embed a ZIP file within a tweetable and "executable" PNG-ZIP polyglot image.](https://github.com/CleasbyCode/pdvzip)
* [jdvrif: CLI tool to encrypt & embed any file type within a JPG image.](https://github.com/CleasbyCode/jdvrif)
* [imgprmt: CLI tool to embed an image prompt (e.g. "Midjourney") within a tweetable JPG-HTML polyglot image.](https://github.com/CleasbyCode/imgprmt)
* [xif: CLI tool to embed small files (e.g. "workflow.json") within a tweetable JPG-ZIP polyglot image.](https://github.com/CleasbyCode/xif)  
* [pdvps: PowerShell / C++ CLI tool to encrypt & embed any file type within a tweetable & "executable" PNG image](https://github.com/CleasbyCode/pdvps)

##
