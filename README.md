# pdvrdt

A *"steganography-like"* command-line utility consisting of two CLI tools, ***pdvin***, *used for embedding a data file within a ***PNG*** cover image*, and ***pdvout***, *used for extracting the hidden file from the cover image.*  

*Try the ***pdvrdt*** Web App [***here***](https://cleasbycode.co.uk/pdvrdt/index/) for a convenient alternative to downloading and compiling the CLI source code. Web file uploads are limited to 20MB.*

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/prdt_68736.png)  
*Image credit: [***hypsen / @___hypseN***](https://x.com/___hypseN) / ***PIN: 1513573452430888550****

Unlike the common steganography method of concealing data within the pixels of a cover image ([***LSB***](https://ctf101.org/forensics/what-is-stegonagraphy/)), ***pdvrdt*** hides files within various ***chunks*** of a ***PNG*** image, such as iCCP and IDAT. You can embed any file type up to ***2GB***, although compatible hosting sites (*listed below*) have their own ***much smaller*** size limits and *other requirements.  

For increased storage capacity and better security, your embedded data file is compressed with ***zlib/deflate*** (*if not already a compressed file type*) and encrypted using the ***libsodium*** cryptographic library.  
## Compatible Platforms
*Posting size limit measured by the combined size of the cover image + compressed data file:* 
 
* ***Flickr*** (**200MB**), ***ImgBB*** (**32MB**), ***PostImage*** (**32MB**), ***Reddit*** (**19MB** | ***-r option***),
* ***Mastodon*** (**16MB** | ***-m option***), ***ImgPile*** (**8MB**), ***X-Twitter*** (**5MB** + ****Dimension limits, see below***)
  
*X-Twitter image dimension size limits:* 
* ****PNG-32/24*** (*Truecolor*) **68x68** Min. - **900x900** Max.
* ***PNG-8*** (*Indexed-color*) **68x68** Min. - **4096x4096** Max.

## Usage (Linux - pdvin / pdvout)

```console
user1@mx:~/Downloads/pdvrdt-main/src/pdvin$ sudo apt-get install libsodium-dev
user1@mx:~/Downloads/pdvrdt-main/src/pdvin$ chmod +x compile_pdvin.sh
user1@mx:~/Downloads/pdvrdt-main/src/pdvin$ ./compile_pdvin.sh
user1@mx:~/Downloads/pdvrdt-main/src/pdvin$ Compilation successful. Executable 'pdvin' created.
user1@mx:~/Downloads/pdvrdt-main/src/pdvin$ sudo cp pdvin /usr/bin

user1@mx:~/Desktop$ pdvin 

Usage: pdvin [-m|-r] <cover_image> <secret_file>  
       pdvin --info

user1@mx:~/Desktop$ pdvin my_cover_image.png document.pdf
  
Saved "file-embedded" PNG image: prdt_17627.png (1245285 bytes).

Recovery PIN: [***3483965536165427463***]

Important: Keep your PIN safe, so that you can extract the hidden file.

Complete!

user1@mx:~/Downloads/pdvrdt-main/src/pdvout$ sudo apt-get install libsodium-dev
user1@mx:~/Downloads/pdvrdt-main/src/pdvout$ chmod +x compile_pdvout.sh
user1@mx:~/Downloads/pdvrdt-main/src/pdvout$ ./compile_pdvout.sh
user1@mx:~/Downloads/pdvrdt-main/src/pdvout$ Compilation successful. Executable 'pdvout' created.
user1@mx:~/Downloads/pdvrdt-main/src/pdvout$ sudo cp pdvout /usr/bin

user1@mx:~/Desktop$ pdvout

Usage: pdvout <file_embedded_image>
       pdvout --info
        
user1@mx:~/Desktop$ pdvout prdt_17627.png

PIN: *******************

Extracted hidden file: document.pdf (1016540 bytes).

Complete! Please check your file.
```
By default (*no options selected*), ***pdvin*** embeds your data file within the last ***IDAT*** chunk of the ***PNG*** image.  
For ***Mastodon***, the data file is stored within the compressed ***iCCP chunk*** of the ***PNG*** image.  

To create "*file-embedded*" ***PNG*** images compatible for posting on ***Mastodon***, use the ***-m*** option with ***pdvin***.

https://github.com/user-attachments/assets/dad59635-c431-4d3a-a2dc-93b9f4f3b6be

https://github.com/user-attachments/assets/7b5a5e80-60fe-46f1-891c-7e5c8d16abbb

To correctly download images from ***X-Twitter*** or ***Reddit***, click the image in the post to ***fully expand it***, before saving.  

In addition to the **5MB** image size limit, ***X-Twitter*** also has image dimension size limits, listed above.
 
https://github.com/user-attachments/assets/aa99cb70-cf11-4bee-8cf5-40a6015c23e6

To create "*file-embedded*" ***PNG*** images ***compatible*** for posting on ***Reddit***, use the ***-r*** option with ***pdvin***.  
From the ***Reddit*** site, click "*Create Post*" then select "*Images & Video*" tab, to post your ***PNG*** image. 

To correctly download an image from ***Flickr***, click the download arrow near the bottom right-hand corner of the page and select ***Original*** for the size of image to download.

## Third-Party Libraries

This project makes use of the following third-party libraries:

- [**libsodium**](https://libsodium.org/) for cryptographic functions.
  - [**LICENSE**](https://github.com/jedisct1/libsodium/blob/master/LICENSE)
  - Copyright (c) 2013-2025 Frank Denis (github@pureftpd.org)
- **zlib**: General-purpose compression library
  - License: zlib/libpng license (see [***LICENSE***](https://github.com/madler/zlib/blob/develop/LICENSE) file)
  - Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler

##
