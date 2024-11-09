# pdvrdt

Use CLI tools ***pdvin*** & ***pdvout*** with a ***PNG*** image, to embed or extract any file, up to **2GB** (cover image + data file).
 
Compatible hosting sites, ***listed below***, have their own ***much smaller*** image size limits:-
* ***Flickr*** (**200MB**), ***ImgBB*** (**32MB**), ***PostImage*** (**32MB**), ***Reddit*** (**19MB** / ***-r option***),
* ***Mastodon*** (**16MB** / ***-m option***), ***ImgPile*** (**8MB**), \****X/Twitter*** (**5MB** + ***Dimension limits***)

*There are many other image hosting sites on the web that may also be compatible.*  

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/prdt_69874.png)  
***Image credit:*** [***@z3pio_***](https://x.com/z3pio_) / ***PIN: 2430293547***

Your embedded data file is ***compressed*** (depending on file type) and ***encrypted***.  
## Usage (Linux - pdvin)

```console

user1@linuxbox:~/Downloads/pdvrdt-main/src/pdvin$ g++ main.cpp -O2 -lz -s -o pdvin
user1@linuxbox:~/Downloads/pdvrdt-main/src/pdvin$ sudo cp pdvin /usr/bin

user1@linuxbox:~/Desktop$ pdvin 

Usage: pdvin [-m|-r] <cover_image> <data_file>  
       pdvin --info

user1@linuxbox:~/Desktop$ pdvin my_cover_image.png document.pdf
  
Saved "file-embedded" PNG image: prdt_17627.png (1245285 bytes).

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

Extracted hidden file: document.pdf (1016540 bytes).

Complete! Please check your file.

```
By default (*no options selected*), ***pdvin*** embeds your data file within the last ***IDAT*** chunk of the ***PNG*** image.  
For ***Mastodon***, the data file is stored within the ***iCCP chunk*** of the ***PNG*** image.  

To create "*file-embedded*" ***PNG*** images compatible for posting on ***Mastodon***, use the ***-m*** option with ***pdvin***.

https://github.com/user-attachments/assets/2ccc0cb7-f308-4df7-8a5a-1f20d453a62a  

To correctly download images from [***X/Twitter***](https://youtu.be/nwqi3WN2lkA) or ***Reddit***, click the image in the post to ***fully expand it***, before saving.  

In addition to the **5MB** image size limit, ***X/Twitter*** also has the following dimension size limits.
* ***PNG-32/24*** (*Truecolor*) **68x68** Min. - **900x900** Max.
* ***PNG-8*** (*Indexed-color*) **68x68** Min. - **4096x4096** Max. 
 
To create "*file-embedded*" ***PNG*** images ***compatible*** for posting on [***Reddit***](https://youtu.be/JAlo0I3hbPY), use the ***-r*** option with ***pdvin***.  
From the ***Reddit*** site, click "*Create Post*" then select "*Images & Video*" tab, to post your ***PNG*** image. 

To correctly download an image from [***Flickr***](https://youtu.be/uE_cn6sMwK4), click the download arrow near the bottom right-hand corner of the page and select ***Original*** for the size of image to download.
  
You can try ***pdvrdt*** from [**this site**](https://cleasbycode.co.uk/pdvrdt/index/) if you don't want to download and compile the source code.

https://github.com/user-attachments/assets/6cb2f679-559c-49dc-99b6-716de3d7af3c

**ImgPile -** *You must sign in to an account before sharing your data-embedded PNG image on ImgPile*.  
		*Sharing your image without logging in, your embedded data will not be preserved.*

 My other programs you may find useful:-
 
* [pdvzip: CLI tool to embed a ZIP file within a tweetable and "executable" PNG-ZIP polyglot image.](https://github.com/CleasbyCode/pdvzip)
* [jdvrif: CLI tool to encrypt & embed any file type within a JPG image.](https://github.com/CleasbyCode/jdvrif)
* [imgprmt: CLI tool to embed an image prompt (e.g. "Midjourney") within a tweetable JPG-HTML polyglot image.](https://github.com/CleasbyCode/imgprmt)
* [pdvps: PowerShell / C++ CLI tool to encrypt & embed any file type within a tweetable & "executable" PNG image](https://github.com/CleasbyCode/pdvps)

##
