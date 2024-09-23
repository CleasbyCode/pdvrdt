# pdvrdt

Use CLI tools ***pdvin*** & ***pdvout*** with a ***PNG*** image, to embed or extract any file, up to ***2GB** (cover image + data file).
 
****Compatible hosting sites, listed below, have their own much smaller image size limits:***
* ***Flickr*** (**200MB**), ***ImgBB*** (**32MB**), ***PostImage*** (**32MB**), ***Reddit*** (**19MB** / ***-r option***),
* ***Mastodon*** (**16MB** / ***-m option***), ***ImgPile*** (**8MB**), \****X/Twitter*** (**5MB** + ***Dimension limits***)

*There are many other image hosting sites on the web that may also be compatible.*  

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/prdt_11298.png)  
***Image credit:*** [***@z3pio_***](https://x.com/z3pio_)

Demo Videos: [***X/Twitter***](https://youtu.be/nwqi3WN2lkA) / [***Mastodon***](https://youtu.be/A7c06xdcLRQ) / [***Reddit***](https://youtu.be/pp9-Nk0VslA) / [***Web Tool***](https://youtu.be/KbIilEDF14E)

To ***share*** *"file-embedded"* ***PNG*** images on ***Mastodon***, use the ***-m*** option with ***pdvin***.  

To ***share*** *"file-embedded"* ***PNG*** images on ***Reddit***, use the ***-r*** option with ***pdvin***.  
Select the "***Images & Video***" tab on ***Reddit*** to post your image.  

***X/Twitter** also has dimension size limits:-

 ***PNG-32/24*** (*Truecolor*) **900x900** Max. **68x68** Min.  
 ***PNG-8*** (*Indexed-color*) **4096x4096** Max. **68x68** Min.  

To correctly download images from ***X/Twitter*** or ***Reddit***, click the image in the post to ***fully expand it***, before saving.  

To correctly download an image from ***Flickr***, click the download arrow near the bottom right-hand corner of the site and select ***Original*** for the size of image to download.
  
You can try ***pdvrdt*** from [**this site**](https://cleasbycode.co.uk/pdvrdt/index/) if you don't want to download and compile the source code.

## Usage (Linux - pdvin)

```console

user1@linuxbox:~/Downloads/pdvrdt-main/src/pdvin$ g++ main.cpp -O2 -lz -s -o pdvin
user1@linuxbox:~/Downloads/pdvrdt-main/src/pdvin$ sudo cp pdvin /usr/bin

user1@linuxbox:~/Desktop$ pdvin 

Usage: pdvin [-m|-r] <cover_image> <data_file>  
       pdvin --info

user1@linuxbox:~/Desktop$ pdvin my_cover_image.png document.pdf
  
Saved "file-embedded" PNG image: prdt_17627.png 1245285 Bytes.

Complete!

```
## Usage (Linux - pdvout)

```console

user1@linuxbox:~/Downloads/pdvrdt-main/src/pdvout$ g++ main.cpp -O2 -lz -s -o pdvout
user1@linuxbox:~/Downloads/pdvrdt-main/src/pdvout$ sudo cp pdvout /usr/bin

user1@linuxbox:~/Desktop$ pdvout

Usage: pdvout <file_embedded_image>
       pdvout --info
        
user1@linuxbox:~/Desktop$ pdvout prdt_17627.png

Extracted hidden file: document.pdf 1016540 Bytes.

Complete! Please check your file.

```
![Demo Image2](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/screen.png) 
![Demo Image3](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/screen2.png) 

**Issues:**
* **ImgPile -** *You must sign in to an account before sharing your data-embedded PNG image on ImgPile*.  
		*Sharing your image without logging in, your embedded data will not be preserved.*

 My other programs you may find useful:-
 
* [pdvzip: CLI tool to embed a ZIP file within a tweetable and "executable" PNG-ZIP polyglot image.](https://github.com/CleasbyCode/pdvzip)
* [jdvrif: CLI tool to encrypt & embed any file type within a JPG image.](https://github.com/CleasbyCode/jdvrif)
* [imgprmt: CLI tool to embed an image prompt (e.g. "Midjourney") within a tweetable JPG-HTML polyglot image.](https://github.com/CleasbyCode/imgprmt)
* [jzp: CLI tool to embed small files (e.g. "workflow.json") within a tweetable JPG-ZIP polyglot image.](https://github.com/CleasbyCode/jzp)  
* [pdvps: PowerShell / C++ CLI tool to encrypt & embed any file type within a tweetable & "executable" PNG image](https://github.com/CleasbyCode/pdvps)

##
