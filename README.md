# pdvrdt

PNG Data Vehicle for **Reddit**, (PDVRDT v1.0).

Insert / extract arbitrary data of up to ~1MB within a PNG image.  
You can then upload and share your data embedded image file on **Reddit**. 

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/image_demo.png)  
{***Image demo: Rar archive (containing 7 jpg images) embedded within this PNG image***} 

[**Video Demo (YouTube)**](https://www.youtube.com/watch_popup?v=3vGNwpv3smo)  
[**Image Demo (Reddit) - Image contains PDF document**](https://i.redd.it/rx8cr47bfxma1.png) 

***Note: PDVRDT data inserted images will not work with Twitter.  For Twitter, please use [PDVZIP](https://github.com/CleasbyCode/pdvzip)***

This program works on Linux and Windows.

PDVRDT currently requires the external program '***zlib-flate***'.

You can install **zlib-flate** for **Linux** with '***apt install qpdf***'.  

For **Windows**, you can download the installer from [***Sourceforge***](https://sourceforge.net/projects/qpdf/).  
Once installed, add the path to your environment variables system path (e.g. **C:\Program Files\qpdf 11.2.0\bin**).
 
1,048,444 bytes is the (zlib) uncompressed limit for your arbitrary data.  
132 bytes is used for the barebones iCCP profile. (132 + 1048444 = 1,048,576 [1MB]).

To maximise the amount of data you can embed in your image file, I recommend first compressing your 
data file(s) to zip, rar, formats.  Make sure the zip/rar compressed file does not exceed 1,048,444 bytes.

Your file will be compressed again in zlib format when embedded into the image file (iCCP profile chunk).

Compile and run the program under Windows or **Linux**.

## Usage (Linux - Inserting data file into PNG image)

```c
$ g++ pdvrdt.cpp -o pdvrdt
$
$ ./pdvrdt

Usage:-
	Insert:  pdvrdt  <png_image>  <your_file>
	Extract: pdvrdt  <png_image>
	Help:	 pdvrdt  --info

$ ./pdvrdt boat.png document.pdf

All done!  
  
Created output file: 'pdvrdt_image.png'
You can now upload and share this PNG image on Reddit.

```
## Usage (Linux - Extacting data file from PNG image)

```c
$ ./pdvrdt

Usage:-
	Insert:  pdvrdt  <png_image>  <your_file>
	Extract: pdvrdt  <png_image>
	Help:	 pdvrdt  --info
        
$ ./pdvrdt pdvrdt_image_file.png

All done!

Created output file: 'pdvrdt_extracted_file.pdf'

```

Other editions of **pdv** you may find useful:-  

* [pdvzip - PNG Data Vehicle for Twitter, ZIP Edition](https://github.com/CleasbyCode/pdvzip)  
* [pdvps - PNG Data Vehicle for Twitter, PowerShell Edition](https://github.com/CleasbyCode/pdvps)   

##
