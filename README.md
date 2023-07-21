# pdvrdt

PNG Data Vehicle for **Reddit**, (pdvrdt v1.3).

Embed & extract files (up to ~1MB) via a PNG image.  
Post & share your "*file-embedded*" image on *Reddit (*Desktop / Desktop mode only. Does not work with mobile app*).

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/source.png)  
{***Image demo (github): ZIP archive embedded within this PNG (contains the source code for this repo)***} 

[**Video_1 (YouTube) - Embed Image Prompt & Share Image on Reddit. Download Image & Extract Prompt.**](https://youtu.be/HqBbsCjenZQ)  
[**Video_2 (YouTube) - Download & Extract Multipart RAR File (MP3).**](https://youtu.be/SHElh8VJ3ZQ)  

***Note: pdvrdt "file-embedded" images will also work with Twitter if your embedded file is 10KB or lower.
	 To embed larger files for Twitter (up to 5MB), please use [pdvzip](https://github.com/CleasbyCode/pdvzip)***

This program can be used on Linux and Windows.
 
1,048,444 bytes is the inflated (uncompressed) limit for your data file.  
132 bytes is used for the basic iCCP profile. (132 + 1048444 = 1,048,576 [1MB]).

To maximise the amount of data you can embed in your image file, I recommend first compressing your 
data file(s) to zip/rar formats, etc.  Make sure the zip/rar compressed file does not exceed 1,048,444 bytes.

Your file will be encrypted, deflate/compressed (zlib) and embedded into the image file.

You can insert up to five files at a time (outputs one image per file).  
You can extract files from up to five images at a time.

Compile and run the program under Windows or **Linux**.

## Usage (Linux - Insert file into PNG image / Extract file from image)

```c

$ g++ pdvrdt.cpp -lz -s -o pdvrdt
$
$ ./pdvrdt 

Usage:  pdvrdt -i <png-image>  <file(s)>  
	pdvrdt -x <png-image(s)>  
	pdvrdt --info

$ ./pdvrdt -i image.png  document.pdf
  
Created output file: "pdvimg1.png"  

Complete!  

You can now post your file-embedded PNG image(s) on reddit.  

$ ./pdvrdt

Usage:  pdvrdt -i <png-image>  <file(s)>  
	pdvrdt -x <png-image(s)>  
	pdvrdt --info
        
$ ./pdvrdt -x pdvimg1.png

Created output file: "pdv_document.pdf"  

Complete!  

$ ./pdvrdt -i czar_music.png  czar.part1.rar czar.part2.rar czar.part3.rar  

Created output file: "pdvimg1.png"

Created output file: "pdvimg2.png"

Created output file: "pdvimg3.png"

Complete!

You can now post your file-embedded PNG image(s) on reddit.  

$ ./pdvrdt -x pdvimg1.png pdvimg2.png pdvimg3.png  

Created output file: "pdv_czar.part1.rar"

Created output file: "pdv_czar.part2.rar"

Created output file: "pdv_czar.part3.rar"  

Complete!

```

 My other programs you may find useful:-

* [jdvrif - JPG Data Vehicle for Reddit, Imgur, Flickr & Other Compatible Social Media / Image Hosting Sites.](https://github.com/CleasbyCode/jdvrif)
* [pdvzip - PNG Data Vehicle (ZIP Edition) for Compatible Social Media & Image Hosting Sites.](https://github.com/CleasbyCode/pdvzip)
* [imgprmt - Embed image prompts as a basic HTML page within a JPG image file](https://github.com/CleasbyCode/imgprmt)
* [pdvps - PNG Data Vehicle for Twitter, PowerShell Edition](https://github.com/CleasbyCode/pdvps)   

##
