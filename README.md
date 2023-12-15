# pdvrdt

A simple command-line tool used to embed or extract any file type via a PNG image.  

You can share your *file-embedded* PNG image(s) on the following compatible sites.

* ***Flickr (200MB), ImgBB (32MB), PostImage (24MB), \*Reddit (20MB / with -r option)***, 
* ***Mastodon (16MB), \*ImgPile (8MB), Imgur (5MB), \*Reddit (1MB), Twitter (10KB).***

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/pdv_img1.png)  
Image Credit: [MΞV.ai / @aest_artificial](https://twitter.com/aest_artificial)  
{***Image contains an embedded MP3 file. Extract: pdvrdt -x pdv_img1.png***} 

Demo Videos: [**Mastodon**](https://youtu.be/veODZ_xaBDQ) / [**Reddit**](https://youtu.be/p34bii_b8n4)  
 
For **Mastodon** (*requires the -m option*) your data file is encrypted & compressed (zlib deflate) and stored within the ***iCCP chunk***, 
of the PNG image file.

![profile Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/icc_rdt.png)  

You can insert up to ***six*** files at a time (outputs one image per file).  
You can also extract files from up to ***six*** images at a time.

Compile and run the program under Windows or **Linux**.

## Usage (Linux - Insert file into PNG image / Extract file from image)

```console

user1@linuxbox:~/Desktop$ g++ pdvrdt.cpp -O2 -lz -s -o pdvrdt
user1@linuxbox:~/Desktop$
user1@linuxbox:~/Desktop$ ./pdvrdt 

Usage: pdvrdt -e [-m] [-i] <png_image> <file(s)>  
       pdvrdt -x <png_image(s)>  
       pdvrdt --info

user1@linuxbox:~/Desktop$ ./pdvrdt -e rabbit.png document.pdf
  
Embed mode selected.

Reading files. Please wait...

Encrypting data file.

Compressing data file.

Embedding data file within the last IDAT chunk of the PNG image.

Writing file-embedded PNG image out to disk.

Created file-embedded PNG image: "pdv_img1.png" Size: "1245285 Bytes".

Complete!

You can now post your file-embedded PNG image(s) to the relevant supported platforms.

user1@linuxbox:~/Desktop$ ./pdvrdt

Usage: pdvrdt -e [-m] [-i] <png_image> <file(s)>  
       pdvrdt -x <png_image(s)>  
       pdvrdt --info
        
user1@linuxbox:~/Desktop$ ./pdvrdt -x pdv_img1.png

eXtract mode selected.

Reading embedded PNG image file. Please wait...

Found compressed IDAT chunk.

Inflating IDAT chunk.

Found pdvrdt embedded data file.

Extracting encrypted data file from the iCCP chunk.

Decrypting extracted data file.

Writing decrypted data file out to disk.

Saved file: "document.pdf" Size: "1016540 Bytes"

Complete! Please check your extracted file(s).
  
user1@linuxbox:~/Desktop$ 

```
**Issues:**
* **Reddit -** *Does not work with Reddit's mobile app. Desktop/browser only.*
* **ImgPile -** *You must sign in to an account before sharing your data-embedded PNG image on ImgPile*.  
		*Sharing your image without logging in, your embedded data will not be preserved.*

 My other programs you may find useful:-
 
* [pdvzip: CLI tool to embed a ZIP file within a tweetable and "executable" PNG-ZIP polyglot image.](https://github.com/CleasbyCode/pdvzip)
* [jdvrif: CLI tool to encrypt & embed any file type within a JPG image.](https://github.com/CleasbyCode/jdvrif)
* [imgprmt: CLI tool to embed an image prompt (e.g. "Midjourney") within a tweetable JPG-HTML polyglot image.](https://github.com/CleasbyCode/imgprmt)
* [jzp: CLI tool to embed small files (e.g. "workflow.json") within a tweetable JPG-ZIP polyglot image.](https://github.com/CleasbyCode/jzp)  
* [pdvps: PowerShell / C++ CLI tool to encrypt & embed any file type within a tweetable & "executable" PNG image](https://github.com/CleasbyCode/pdvps)

##
