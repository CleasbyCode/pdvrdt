# pdvrdt

PNG Data Vehicle for ***Mastodon*** & ***Reddit***  

This command line tool enables you to embed & extract any file type via a PNG image.  
You can share your embedded image file on Mastodon, 16MB max. or Reddit, 1MB max.  

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/Witch_Image.png)  
Image Credit: [ZOOT / @LittleTinRobot](https://twitter.com/LittleTinRobot/status/1689155758129336320)  
{***Image contains an embedded MP3 file (7MB) / extract: pdvrdt -x Witch_Image.png)***} 

[**Video Demo 1: Mastodon**](https://youtu.be/-zFJcljHzZU)   
[**Video Demo 2: Reddit**](https://youtu.be/SHElh8VJ3ZQ)  

***If your data file is under ***10KB*** and image dimensions 900x900 or less, you can also  
share your "***file-embedded***" PNG image on ***Twitter***.***  

Other image hosting sites may also be compatible with pdvrdt embedded images.

To embed larger files for ***Twitter*** (***5MB max.***), please use **[pdvzip](https://github.com/CleasbyCode/pdvzip)**  
To embed larger files for ***Reddit*** (***20MB max.***), please use **[jdvrif](https://github.com/CleasbyCode/jdvrif)**

You can also use ***[jdvrif](https://github.com/CleasbyCode/jdvrif)*** for ***Mastodon***.  

For ***Mastodon***, the ****16MB*** size limit is measured by the total size of your ***"file-embedded"*** PNG image.  

***Note: While techinically the image size limit for Mastodon is 16MB, embedded PNG images over 10MB  
are slower to post and may not always upload. [jdvrif](https://github.com/CleasbyCode/jdvrif) is faster and more reliable for larger files.***

The graphics editor program, GIMP, will not display pdvrdt embedded PNG image files over 7MB.

For ***Reddit*** and ***Twitter***, the size limit is measured by the uncompressed size of the ***ICC Profile***,  
where your data is stored (encrypted & compressed).  

![profile Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/profile.png)  

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

```c

$ g++ pdvrdt.cpp -lz -s -o pdvrdt
$
$ ./pdvrdt 

Usage:  pdvrdt -i <png-image> <file(s)>  
	pdvrdt -x <png-image(s)>  
	pdvrdt --info

$ ./pdvrdt -i image.png document.pdf
  
Created output file: "pdvimg1.png"  

Complete!  

You can now post your "file-embedded" PNG image(s) to the relevant supported platforms.

$ ./pdvrdt

Usage:  pdvrdt -i <png-image> <file(s)>  
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

You can now post your "file-embedded" PNG image(s) to the relevant supported platforms.

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
