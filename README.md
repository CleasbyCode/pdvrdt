# pdvrdt

A fast steganography command-line tool used for embedding and extracting any file type via a **PNG** cover image.  

There is also a [***Web edition***](https://cleasbycode.co.uk/pdvrdt/app/), which you can use immediately, as a convenient alternative to downloading and compiling the CLI source code. Web file uploads are limited to **20MB**.    

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/prdt_80983.png)  
*Image: "Wolf" / ***PIN: 1816136426548255229****

Unlike the common steganography method of concealing data within the pixels of a cover image ([***LSB***](https://ctf101.org/forensics/what-is-stegonagraphy/)), ***pdvrdt*** hides files within various ***chunks*** of a ***PNG*** image, such as iCCP and IDAT. You can embed any file type up to ***2GB***, although compatible hosting sites (*listed below*) have their own ***much smaller*** size limits and *other requirements.  

For increased storage capacity and better security, your embedded data file is compressed with ***Zlib*** (*if not already a compressed file type*) and encrypted using the ***Libsodium*** cryptographic library.  

https://github.com/user-attachments/assets/76732196-815b-45ac-b71d-6e1aca672e25  

https://github.com/user-attachments/assets/7a1557d1-4772-4d3d-94a6-ec58d0977a59  

*Image credit: ***"Red_Dragon / [@ultra_arcane](https://x.com/ultra_arcane)"****

## Compatible Platforms
*Posting size limit measured by the combined size of the cover image + compressed data file:* 
 
* ***Flickr*** (**200MB**), ***ImgBB*** (**32MB**), ***PostImage*** (**32MB**), ***Reddit*** (**19MB** | ***-r option***),
* ***Mastodon*** (**16MB** | ***-m option***), ***ImgPile*** (**8MB**), ***X-Twitter*** (**5MB** + ****Dimension limits, see below***)
  
*X-Twitter image dimension size limits:* 
* ****PNG-32/24*** (*Truecolor*) **68x68** Min. - **900x900** Max.
* ***PNG-8*** (*Indexed-color*) **68x68** Min. - **4096x4096** Max.

## Usage (Linux)

```console

user1@mx:~/Downloads/pdvrdt-main/src$ sudo apt install libsodium-dev
user1@mx:~/Downloads/pdvrdt-main/src$ chmod +x compile_pdvrdt.sh
user1@mx:~/Downloads/pdvrdt-main/src$ ./compile_pdvrdt.sh
user1@mx:~/Downloads/pdvrdt-main/src$ Compiling pdvrdt...
user1@mx:~/Downloads/pdvrdt-main/src$ Compilation successful. Executable 'pdvrdt' created.
user1@mx:~/Downloads/pdvrdt-main/src$ sudo cp pdvrdt /usr/bin

user1@mx:~/Desktop$ pdvrdt 

Usage: pdvrdt conceal [-m|-r] <cover_image> <secret_file>
       pdvrdt recover <cover_image>  
       pdvrdt --info

user1@mx:~/Desktop$ pdvrdt conceal your_cover_image.png your_secret_file.doc

Platform compatibility for output image:-

  ✓ X-Twitter
  ✓ ImgPile
  ✓ PostImage
  ✓ ImgBB
  ✓ Flickr
  
Saved "file-embedded" PNG image: prdt_12462.png (143029 bytes).

Recovery PIN: [***2166776980318349924***]

Important: Keep your PIN safe, so that you can extract the hidden file.

Complete!
        
user1@mx:~/Desktop$ pdvrdt recover prdt_12462.png

PIN: *******************

Extracted hidden file: your_secret_file.doc (6165 bytes).

Complete! Please check your file.

```
pdvrdt ***mode*** arguments:
 
  ***conceal*** - Compresses, encrypts and embeds your secret data file within a ***PNG*** cover image.  
  ***recover*** - Decrypts, uncompresses and extracts the concealed data file from a ***PNG*** cover image.
 
pdvrdt ***conceal*** mode platform options:
 
  "***-m***" - To create compatible "*file-embedded*" ***PNG*** images for posting on the ***Mastodon*** platform, you must use the ***-m*** option with ***conceal*** mode.
  ```console
  $ pdvrdt conceal -m my_image.png hidden.doc
  ```
  "***-r***" - To create compatible "*file-embedded*" ***PNG*** images for posting on the ***Reddit*** platform, you must use the ***-r*** option with ***conceal*** mode.
  ```console
  $ pdvrdt conceal -r my_image.png secret.mp3 
   ```
   From the ***Reddit*** site, select "***Create Post***" followed by "***Images & Video***" tab, to attach and post your ***PNG*** image.
    
 To correctly download images from ***X-Twitter*** or ***Reddit***, click the image in the post to fully expand it, before saving.

## Third-Party Libraries

This project makes use of the following third-party libraries:
- **LodePNG** by Lode Vandevenne
  - License: zlib/libpng (see [***LICENSE***](https://github.com/lvandeve/lodepng/blob/master/LICENSE) file)
  - Copyright (c) 2005-2024 Lode Vandevenne
- [**libsodium**](https://libsodium.org/) for cryptographic functions.
  - [**LICENSE**](https://github.com/jedisct1/libsodium/blob/master/LICENSE)
  - Copyright (c) 2013-2025 Frank Denis (github@pureftpd.org)
- **zlib**: General-purpose compression library
  - License: zlib/libpng license (see [***LICENSE***](https://github.com/madler/zlib/blob/develop/LICENSE) file)
  - Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler

##
