# pdvrdt

PNG Data Vehicle for **Reddit**, (PDVRDT v1.0).

Embed & extract arbitrary data of up to ~1MB within a PNG image.  

You can then upload and share your data embedded image file on **Reddit**. 

***PDVRDT data embedded images will not work with Twitter.  For Twitter, please use [PDVZIP](https://github.com/CleasbyCode/pdvzip)***

This program works on Linux and Windows.

PDVRDT currently requires the external program '***zlib-flate***'.

If not already installed, you can install zlib-flate for Linux with '***apt install qpdf***'.  
For Windows you can download the installer from [***Sourceforge***](https://sourceforge.net/projects/qpdf/)
 
1,048,444 bytes is the (zlib) uncompressed limit for your arbitrary data.  
132 bytes is used for the barebones iCCP profile. (132 + 1048444 = 1,048,576 / 1MB).

To maximise the amount of data you can embed in your image file, I recommend compressing your 
data file(s) to zip, rar, etc. Make sure the zip/rar compressed file does not exceed 1,048,444 bytes.

Your file will be compressed again in zlib format when embedded into the image file (iCCP profile chunk).

Compile and run the program under Windows or **Linux**.

## Usage (Linux)

```c
$ g++ pdvrdt.cpp -o pdvrdt
$
$ ./pdvrdt

Usage:-
	Insert:  pdvrdt  <png_image>  <your_file>
	Extract: pdvrdt  <png_image>
	Help:	 pdvrdt  --info

$ ./pdvrdt demo_image.png training_file.pdf

Created output file: PDV_PROFILE_TMP

Using zlib-flate to compress PDV_PROFILE_TMP

zlib-flate created compressed output file: PDV_PROFILE_TMP.z

Created output file: pdv_embedded_image_file.png

All done! You can now upload and share this data embedded PNG image on Reddit.

$ ./pdvrdt

Usage:-
	Insert:  pdvrdt  <png_image>  <your_file>
	Extract: pdvrdt  <png_image>
	Help:	 pdvrdt  --info
        
$ ./pdvrdt pdv_embedded_image_file.png

Created output file: PDV_EMBEDDED_PROFILE_TMP.z

Using zlib-flate to uncompress PDV_EMBEDDED_PROFILE_TMP.z

zlib-flate created uncompressed output file: PDV_EMBEDDED_PROFILE_TMP

Created output file: pdv_extracted_file.pdf

All done! Embedded data file has been extracted from PNG image. Please check your file.

$
```

##
