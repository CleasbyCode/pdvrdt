# pdvrdt

PNG Data Vehicle for **Reddit**, (pdvrdt v1.2).

Embed & extract arbitrary data of up to ~1MB within a PNG image.  
Post & share your "*file-embedded*" image on **[reddit](https://www.reddit.com/)**. 

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/source.png)  
{***Image demo (github): ZIP archive embedded within this PNG (contains the source code for this repo)***} 

[**Video_1 (YouTube) - Embed A.I. Prompt / Post to reddit. Download Image & Extract Prompt.**](https://youtu.be/RS1n2sAITDE)  
[**Video_2 (YouTube) - Download & Extract Multipart RAR File (MP3).**](https://youtu.be/SHElh8VJ3ZQ)  
[**Image Demo (reddit) - Image containing PDF document.**](https://i.redd.it/kxfi6h0rjrqa1.png) 

***Note: pdvrdt file-embedded images do not work with Twitter.  For Twitter, please use [pdvzip](https://github.com/CleasbyCode/pdvzip)***

This program can be used on Linux and Windows.
 
1,048,444 bytes is the inflated/uncompressed (zlib) limit for your arbitrary data.  
132 bytes is used for the barebones iCCP profile. (132 + 1048444 = 1,048,576 [1MB]).

To maximise the amount of data you can embed in your image file, I recommend first compressing your 
data file(s) to zip/rar formats, etc.  Make sure the zip/rar compressed file does not exceed 1,048,444 bytes.

Your file will be encrypted, deflate/compressed (zlib) and embedded into the image file (iCCP Profile chunk).

You can insert & extract up to five files at a time.

Compile and run the program under Windows or **Linux**.

This program has been split into two parts.

1. pdvin - used to insert your file into PNG image.
2. pdvex - used to extract your file from the PNG image.

## Usage (Linux - Compile source / Insert file into PNG image / Extract file from image)

```c
$ g++ pdvin.cpp -lz -o pdvin
$ g++ pdvex.cpp -lz -o pdvex 
$
$ ./pdvin 

Usage:	pdvin  <png_image>  <file_1 ... file_5>  
	pdvin  pdvrdt  --info

$ ./pdvin car.png document.pdf
  
Created output file: "pdv_img_1.png"  

All done!  

You can now post your file-embedded PNG image(s) on reddit.  

$ ./pdvex

Usage:	pdvex  <image_1 ... image_5>
	pdvex  --info
        
$ ./pdvex pdv_img_1.png

Created output file: "pdv_document.pdf"  

All done!

```

Other editions of **pdv** you may find useful:-  

* [pdvzip - PNG Data Vehicle for Twitter, ZIP Edition](https://github.com/CleasbyCode/pdvzip)  
* [pdvps - PNG Data Vehicle for Twitter, PowerShell Edition](https://github.com/CleasbyCode/pdvps)   

##
