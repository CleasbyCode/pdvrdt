# pdvrdt

***pdvrdt*** is a fast, easy-to-use steganography command-line tool used for concealing and extracting any file type via a PNG image.

There is also a [***Web edition***](https://cleasbycode.co.uk/pdvrdt/app/), which you can use immediately, as a convenient alternative to downloading and compiling the CLI source code. Web file uploads are limited to **20MB**.    

An experimental ***Rust*** port [***pdvrdt-rs***](https://github.com/CleasbyCode/pdvrdt-rs) is available for those interested in that language. 

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/prdt_526501.png)  
*Image: "Wolf" / ***PIN: 1856140514119088821****

Unlike the common steganography method of concealing data within the pixels of a cover image ([***LSB***](https://ctf101.org/forensics/what-is-stegonagraphy/)), ***pdvrdt*** mostly hides data within various metadata ***chunks*** of a ***PNG*** image, such as iCCP and IDAT. 

The exception to this is the ***Reddit*** platform conceal mode (-r), where we use the advanced steganography technique of ***content-adaptive spatial embedding in the pixel LSBs (Least Significant Bits)***.  

Each 4 bits of payload is carried by 15 RGB samples drawn from a keyed permutation over the whole image, using ***Hamming-syndrome matrix embedding*** — the syndrome is moved to the target by a single ±1 change (LSB matching, not LSB replacement).  

Which sample to change is chosen by a per-sample distortion cost derived from local image activity and channel sensitivity, so edits land in textured regions rather than flat ones. Where two changes cost less than the one required change, it takes them: measured on a 4.3-megapixel cover, 93% of groups need one change, 0.4% take two, and 6% need none — about 4.25 bits per changed sample.

You can conceal any file type up to ***2GB*** for the default conceal mode, although other platform conceal modes and compatible sites (*listed below*) have their own ***much smaller*** size limits and *other requirements.  

For increased storage capacity and better security, your data file is compressed with ***libdeflate/zlib*** — unless it's already a compressed file type — and encrypted with ***XChaCha20-Poly1305*** using the ***libsodium*** cryptographic library.

## Compilation & Usage (Linux)

```console
$ sudo apt update
$ sudo apt install g++ cmake ninja-build util-linux libsodium-dev zlib1g-dev libdeflate-dev

$ chmod +x compile_pdvrdt.sh
$ ./compile_pdvrdt.sh

$ sudo cp pdvrdt /usr/bin
$ pdvrdt 

Usage: pdvrdt conceal [-m] <cover_image> <secret_file>
       pdvrdt recover <cover_image>  
       pdvrdt --info

$ pdvrdt conceal your_cover_image.png your_secret_file.doc

Platform compatibility for output image:-

  ✓ X-Twitter
  ✓ ImgPile
  ✓ PostImage
  ✓ ImgBB
  ✓ Flickr
  
Saved "file-embedded" PNG image: prdt_531618.png (143029 bytes).

Recovery PIN: [***2166776980318349924***]

Important: Keep your PIN safe, so that you can extract the hidden file.

Complete!
        
$ pdvrdt recover prdt_531618.png

PIN: *******************

Extracted hidden file: your_secret_file.doc (6165 bytes).

Complete! Please check your file.

```
## Compatible Platforms
*Posting size limit measured by the combined size of the cover image + compressed data file:* 
 
* ***Flickr*** (**200MB**), ***ImgBB*** (**32MB**), ***PostImage*** (**32MB**), ***Mastodon*** (**16MB** | ***-m option***),
* ***ImgPile*** (**8MB**), ***X-Twitter*** (**5MB** + ****Dimension limits, see below***)
* ***Reddit*** (***-r option***). While the ***Reddit*** platform has an image upload size limit of **20MB**, the data storage capacity for your cover image is ***much smaller*** and depends on image dimension size:-  

For example, a cover image with **1024x1024** dimensions can store only **~103KB** of data,  
***2048x2048*** can store **~418KB** and an image with **4096x4096** dimensions can store **~1.5MB**. 
  
*X-Twitter image dimension size limits:* 
* ****PNG-32/24*** (*Truecolor*) **68x68** Min. - **900x900** Max.
* ***PNG-8*** (*Indexed-color*) **68x68** Min. - **4096x4096** Max.

https://github.com/user-attachments/assets/76732196-815b-45ac-b71d-6e1aca672e25  

https://github.com/user-attachments/assets/7b448218-fe6a-4c3a-8a02-c126dd4cd830

pdvrdt ***mode*** arguments:
 
  ***conceal*** - Compresses, encrypts and embeds your secret data file within a ***PNG*** cover image.  
  ***recover*** - Decrypts, uncompresses and extracts the concealed data file from a ***PNG*** cover image.
 
pdvrdt ***conceal*** mode platform options:

 "***-r***" To create compatible "*data-conclealed*" ***PNG*** images for posting on the ***Reddit*** platform, you must use the ***-r*** option with ***conceal*** mode.
  ```console
  $ pdvrdt conceal -r my_image.png hidden.txt
```
  These images are only compatible for posting on ***Reddit***. Your embedded data file will be removed if posted on a different platform.  
 
  "***-m***" - To create compatible "*file-embedded*" ***PNG*** images for posting on the ***Mastodon*** platform, you must use the ***-m*** option with ***conceal*** mode.
  ```console
  $ pdvrdt conceal -m my_image.png hidden.doc
  ```   
 To correctly download images from ***X-Twitter*** or ***Reddit***, click the image in the post to fully expand it, before saving.

## Third-Party Software and Assets

  ### Core applications

  - [LodePNG](https://github.com/lvandeve/lodepng) — PNG decoding, encoding, and image processing.
    
     License: [zlib License](https://github.com/lvandeve/lodepng/blob/master/lodepng.h#L1-L22)
    
     Copyright (c) 2005-2026 Lode Vandevenne

  - [libsodium](https://github.com/jedisct1/libsodium) — cryptographic random generation, Argon2id
  key derivation and XChaCha20-Poly1305 secret streams. Dynamically linked as a system library.

     License: [ISC License](https://github.com/jedisct1/libsodium/blob/master/LICENSE)
    
     Copyright (c) 2013–2026 Frank Denis.
     
   - [zlib](https://github.com/madler/zlib) — Streaming zlib compression and decompression. Dynamically linked as a system library.

     License: [zlib License](https://github.com/madler/zlib/blob/develop/LICENSE)
    
     Copyright (C) 1995–2026 Jean-loup Gailly and Mark Adler.

  - [libdeflate](https://github.com/ebiggers/libdeflate) — Fast whole-buffer zlib-format compression. Dynamically linked as a system library.

     License: [MIT](https://github.com/ebiggers/libdeflate/blob/master/COPYING)
    
     Copyright 2016 Eric Biggers.
    
     Copyright 2024 Google LLC.

  ### Incorporated code and assets
  
  - [Compact ICC Profiles](https://github.com/saucecontrol/Compact-ICC-Profiles) — the Mastodon ICC
  carrier template is derived from sRGB-v2-nano.icc.

     License: [CC0 1.0 Universal](https://github.com/saucecontrol/Compact-ICC-Profiles/blob/master/license)

  - [hash-prospector](https://github.com/skeeto/hash-prospector) — the lowbias32 integer hash
  finalizer is used by the palette lookup table.

     License: [Unlicense](https://github.com/skeeto/hash-prospector/blob/master/UNLICENSE)

##
