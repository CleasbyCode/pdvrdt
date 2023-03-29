# pdvrdt

PNG Data Vehicle for **Reddit**, (pdvrdt v1.1).

Embed & extract arbitrary data of up to ~1MB within a PNG image.  
Post & share your file-embedded image on **reddit**. 

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/source.png)  
{***Image demo (github): ZIP archive embedded within this PNG (contains the source code for this repo)***} 

[**Video_1 (YouTube) - Embed A.I. Prompt / Post to reddit. Download Image & Extract Prompt.**](https://youtu.be/RS1n2sAITDE)  
[**Video_2 (YouTube) - Download & Extract Multipart RAR File.**](https://youtu.be/SHElh8VJ3ZQ)  
[**Image Demo (reddit) - Image containing prompt text.**](https://i.redd.it/b5b26dj1ttpa1.png) 

***Note: pdvrdt file-embedded images do not work with Twitter.  For Twitter, please use [pdvzip](https://github.com/CleasbyCode/pdvzip)***

This program can be used on Linux and Windows.

pdvrdt currently requires the external program '***zlib-flate***'.

You can install **zlib-flate** for **Linux** with '***sudo apt install qpdf***'.  

For **Windows**, you can download the installer from [***Sourceforge***](https://sourceforge.net/projects/qpdf/).  
Once installed, add the path to your environment variables system path (e.g. **C:\Program Files\qpdf 11.2.0\bin**).
 
1,048,444 bytes is the inflated/uncompressed (zlib) limit for your arbitrary data.  
132 bytes is used for the barebones iCCP profile. (132 + 1048444 = 1,048,576 [1MB]).

To maximise the amount of data you can embed in your image file, I recommend first compressing your 
data file(s) to zip/rar formats, etc.  Make sure the zip/rar compressed file does not exceed 1,048,444 bytes.

Your file will be encrypted, deflated/compressed (zlib) and embedded into the image file (iCCP Profile chunk).

You can extract embedded content from up to five images at a time.

Compile and run the program under Windows or **Linux**.

## Usage (Linux - Inserting data file into PNG image)

```c
$ g++ pdvrdt.cpp -o pdvrdt
$
$ sudo apt install qpdf
$ 
$ ./pdvrdt

Usage:-
	Insert:  pdvrdt  <png_image>  <your_file>
	Extract: pdvrdt  <image1>  <image2> ... <image5>
	Help:	 pdvrdt  --info

$ ./pdvrdt boat.png document.pdf
  
Created output file: "pdvrdt_image.png"
You can now post your file-embedded PNG image on reddit.

All done! 

```
## Usage (Linux - Extracting data file from PNG image)

```c
$ ./pdvrdt

Usage:-
	Insert:  pdvrdt  <png_image>  <your_file>
	Extract: pdvrdt  <image1>  <image2> ... <image5>
	Help:	 pdvrdt  --info
        
$ ./pdvrdt pdvrdt_image.png

Created output file: "pdvrdt_document.pdf"  

All done!

```

Other editions of **pdv** you may find useful:-  

* [pdvzip - PNG Data Vehicle for Twitter, ZIP Edition](https://github.com/CleasbyCode/pdvzip)  
* [pdvps - PNG Data Vehicle for Twitter, PowerShell Edition](https://github.com/CleasbyCode/pdvps)   

##
