# pdvrdt

A simple command-line tool to embed and extract any file type via a PNG image.  

Share your *file-embedded* image on the following compatible sites.  

* ***Flickr (200MB), ImgBB (32MB), PostImage (24MB)***, Mastodon (16MB),
* ***\*ImgPile (8MB), Imgur (5MB), \*Reddit (1MB / Browser only)***.

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/witch_pic.png)  
Image Credit: [ZOOT / @LittleTinRobot](https://twitter.com/LittleTinRobot/status/1689155758129336320)  
{***Image contains an embedded MP3 file (7MB) / extract: pdvrdt -x witch_pic.png)***} 

[**Video Demo 1: Mastodon**](https://youtu.be/-zFJcljHzZU)   
[**Video Demo 2: Reddit**](https://youtu.be/j7BC31nVrMg)  

**\*ImgPile** - *You must sign in to an account before sharing your data-embedded PNG image on this platform. Sharing your image
without logging in, your embedded data will not be preserved.*

***If your data file is under ***10KB*** and image dimensions 900x900 or less (PNG-32/24),  
4096x4096 or less (PNG-8), you can also share your *file-embedded* PNG image on ***Twitter***.***  

To embed larger files for ***Twitter*** (***5MB max.***), please try **[pdvzip](https://github.com/CleasbyCode/pdvzip)**  
To embed larger files for ***Reddit*** (***20MB max.***), please try **[jdvrif](https://github.com/CleasbyCode/jdvrif)**

You can also use ***[jdvrif](https://github.com/CleasbyCode/jdvrif)*** for ***Mastodon***.  

For ***Flickr, ImgBB, PostImage, Mastodon, ImgPile & Imgur***,  
the size limit is measured by the total size of your *file-embedded* PNG image.  

For ***Reddit*** and ***Twitter***, the size limit is measured by the uncompressed size (zlib inflate)  
of the ***iCCP chunk***, where your data is stored (encrypted & compressed).  

![profile Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/iccp_chunk.png)  

***Reddit***: 1,048,172 bytes is the uncompressed (zlib inflate) size limit for your data file.  
404 bytes is used for the basic ***iCC Profile***. (404 + 1048172 = 1,048,576 bytes [***1MB***]).

***Twitter***: 9,836 bytes is the uncompressed (zlib inflate) limit for your data file.  
404 bytes is used for the basic ***iCC Profile*** (404 + 9836 = 10,240 bytes [***10KB***])

To maximise the amount of data you can embed in your image file for ***Reddit*** or ***Twitter***,  
first compress the data file to a ***ZIP*** or ***RAR*** file. Make sure the compressed ***ZIP*** or ***RAR***  
file does not exceed 1,048,172 bytes for ***Reddit*** or 9,836 bytes for ***Twitter***. 

You can insert up to ***six*** files at a time (outputs one image per file).  
You can also extract files from up to ***six*** images at a time.

Compile and run the program under Windows or **Linux**.

## Usage (Linux - Insert file into PNG image / Extract file from image)

```bash

$ g++ pdvrdt.cpp -O2 -lz -s -o pdvrdt
$
$ ./pdvrdt 

Usage:  pdvrdt -i <png-image> <file(s)>  
	pdvrdt -x <png-image(s)>  
	pdvrdt --info

$ ./pdvrdt -i image.png document.pdf
  
Created output file: "pdv_img1.png 912426 Bytes"  

Complete!  

You can now post your "file-embedded" PNG image(s) to the relevant supported platforms.

$ ./pdvrdt

Usage:  pdvrdt -i <png-image> <file(s)>  
	pdvrdt -x <png-image(s)>  
	pdvrdt --info
        
$ ./pdvrdt -x pdv_img1.png

Extracted file: "document.pdf 911314 Bytes"  

Complete!  

$ ./pdvrdt -i czar_music.png  czar.part1.rar czar.part2.rar czar.part3.rar  

Created output file: "pdv_img1.png 777307 Bytes"

Created output file: "pdv_img2.png 777307 Bytes"

Created output file: "pdv_img3.png 777307 Bytes"

Complete!

You can now post your "file-embedded" PNG image(s) to the relevant supported platforms.

$ ./pdvrdt -x pdv_img1.png pdv_img2.png pdv_img3.png  

Extracted file: "czar.part1.rar 776302 Bytes"

Extracted file: "czar.part2.rar 776302 Bytes"

Extracted file: "czar.part3.rar 776302 Bytes"  

Complete!

```

 My other programs you may find useful:-
 
* [pdvzip: CLI tool to embed a ZIP file within a tweetable and "executable" PNG-ZIP polyglot image.](https://github.com/CleasbyCode/pdvzip)
* [jdvrif: CLI tool to encrypt & embed any file type within a JPG image.](https://github.com/CleasbyCode/jdvrif)
* [imgprmt: CLI tool to embed an image prompt (e.g. "Midjourney") within a tweetable JPG-HTML polyglot image.](https://github.com/CleasbyCode/imgprmt)
* [xif: CLI tool to embed small files (e.g. "workflow.json") within a tweetable JPG-ZIP polyglot image.](https://github.com/CleasbyCode/xif)  
* [pdvps: PowerShell / C++ CLI tool to encrypt & embed any file type within a tweetable & "executable" PNG image](https://github.com/CleasbyCode/pdvps)

##
