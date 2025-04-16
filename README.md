# pdvrdt

***pdvrdt*** is a *"steganography-like"* utility for ***Linux*** and ***Windows***. It consists of two CLI tools, ***pdvin***, *used for embedding a data file within a ***PNG*** cover image*, and ***pdvout***, *used for extracting the hidden file from the cover image.*  

Unlike the common steganography method of concealing data within the pixels of a cover image ([***LSB***](https://ctf101.org/forensics/what-is-stegonagraphy/)), ***pdvrdt*** hides files within various ***chunks*** of a ***PNG*** image, such as iCCP and IDAT. You can embed any file type up to ***2GB***, although compatible hosting sites (*listed below*) have their own ***much smaller*** size limits and *other requirements.  

For increased storage capacity and better security, your embedded data file is compressed with ***zlib/deflate*** (*if not already a compressed file type*) and encrypted using the ***libsodium*** cryptographic library.  
 
* ***Flickr*** (**200MB**), ***ImgBB*** (**32MB**), ***PostImage*** (**32MB**), ***Reddit*** (**19MB** / ***-r option***),
* ***Mastodon*** (**16MB** / ***-m option***), ***ImgPile*** (**8MB**), ***X/Twitter*** (**5MB** + ****Dimension limits***)
* ****PNG-32/24*** (*Truecolor*) **68x68** Min. - **900x900** Max. | ***PNG-8*** (*Indexed-color*) **68x68** Min. - **4096x4096** Max.

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/prdt_62941.png)  
***Image/video credit:*** [***@pixelbouncer***](https://x.com/pixelbouncer) / ***PIN: 1787925324565971918***

For extra security, your data file is also compressed (*zlib*) and encrypted using the ***libsodium*** crypto library. 

(*You can try the [***pdvrdt Web App, here,***](https://cleasbycode.co.uk/pdvrdt/index/) if you don't want to download and compile the CLI source code.*)

## Usage (Linux - pdvin / pdvout)

```console
user1@linuxbox:~/Downloads/pdvrdt-main/src/pdvin$ sudo apt-get install libsodium-dev
user1@linuxbox:~/Downloads/pdvrdt-main/src/pdvin$ g++ main.cpp -O2 -lz -lsodium -s -o pdvin
user1@linuxbox:~/Downloads/pdvrdt-main/src/pdvin$ sudo cp pdvin /usr/bin

user1@linuxbox:~/Desktop$ pdvin 

Usage: pdvin [-m|-r] <cover_image> <secret_file>  
       pdvin --info

user1@linuxbox:~/Desktop$ pdvin my_cover_image.png document.pdf
  
Saved "file-embedded" PNG image: prdt_17627.png (1245285 bytes).

Recovery PIN: [***3483965536165427463***]

Important: Keep your PIN safe, so that you can extract the hidden file.

Complete!

user1@linuxbox:~/Downloads/pdvrdt-main/src/pdvin$ g++ main.cpp -O2 -lz -lsodium -s -o pdvout
user1@linuxbox:~/Downloads/pdvrdt-main/src/pdvout$ sudo cp pdvout /usr/bin

user1@linuxbox:~/Desktop$ pdvout

Usage: pdvout <file_embedded_image>
       pdvout --info
        
user1@linuxbox:~/Desktop$ pdvout prdt_17627.png

PIN: *******************

Extracted hidden file: document.pdf (1016540 bytes).

Complete! Please check your file.
```
By default (*no options selected*), ***pdvin*** embeds your data file within the last ***IDAT*** chunk of the ***PNG*** image.  
For ***Mastodon***, the data file is stored within the compressed ***iCCP chunk*** of the ***PNG*** image.  

To create "*file-embedded*" ***PNG*** images compatible for posting on ***Mastodon***, use the ***-m*** option with ***pdvin***.

https://github.com/user-attachments/assets/dad59635-c431-4d3a-a2dc-93b9f4f3b6be

https://github.com/user-attachments/assets/2776d34e-e220-4d7c-955f-04c7cf7ecc1f

To correctly download images from ***X/Twitter*** or ***Reddit***, click the image in the post to ***fully expand it***, before saving.  

In addition to the **5MB** image size limit, ***X/Twitter*** also has the following dimension size limits.
* ***PNG-32/24*** (*Truecolor*) **68x68** Min. - **900x900** Max.
* ***PNG-8*** (*Indexed-color*) **68x68** Min. - **4096x4096** Max. 

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
