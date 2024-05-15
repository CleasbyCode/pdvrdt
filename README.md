# pdvrdt
Use these two command-line tools to embed (*pdvin*) or extract (*pdvout*) any file type via a **PNG** image.  
You can post your image with the hidden file on *X/Twitter and a few other social media sites.

**Image size limits (cover image + data file):*

* *Flickr (200MB), ImgBB (32MB), PostImage (24MB), Reddit (19MB / -r option)*
* *Mastodon (16MB / -m option), ImgPile (8MB), \*X/Twitter (5MB + Dimension limits)*

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/prdt_1128.png)  

Demo Videos: [**Twitter**](https://youtu.be/wSkP7LU7woQ) / [**Mastodon**](https://youtu.be/2giS6rP8dvI) / [**Reddit**](https://youtu.be/7-ZbXv8NqA0) / [**PDVRDT Web**](https://youtu.be/KbIilEDF14E)

*To post file-embedded PNG images on **Mastodon**, you need to use the **-m** option.*  

*To post file-embedded PNG images on **Reddit**, you need to use the **-r** option.*  

From the **Reddit** site, always select the "***Images & Video***" tab/box to post your image.

When saving an image from **Reddit**, use *new.reddit.com*. Click the image in the post to expand it, then save it.  
(*Make sure you see the filename with a **.png** extension in the address bar of your browser, before saving.*)

As well as the 5MB image size limit, **X/Twitter** also has dimension size limits.  

*PNG-32/24 (Truecolor) 900x900 Max. 68x68 Min.*  
*PNG-8 (Indexed-color) 4096x4096 Max. 68x68 Min.*  

*When saving an image from **X/Twitter**, always click the image first to fully expand it, before saving.*  

Compile and run the programs under Windows or **Linux**.  

*(You can try **pdvrdt** from this [**site**](https://cleasbycode.co.uk/pdvrdt/index/) if you don't want to download & compile the source code.)*

## Usage (Linux - Insert file into PNG image / Extract file from image)

```console

user1@linuxbox:~/Desktop$ g++ main.cpp -O2 -lz -s -o pdvin
user1@linuxbox:~/Desktop$
user1@linuxbox:~/Desktop$ pdvrdt 

Usage: pdvin [-m] [-r] <cover_image> <data_file>  
       pdvin --info

user1@linuxbox:~/Desktop$ pdvin rabbit.png document.pdf
  
Writing file-embedded PNG image out to disk.

Created PNG image: prdt_17627.png 1245285 Bytes.

Complete!

You can now post your file-embedded PNG image to the relevant supported platforms.

user1@linuxbox:~/Desktop$ pdvout

Usage: pdvout <cover_image>
       pdvout --info
        
user1@linuxbox:~/Desktop$ pdvout prdt_17627.png

Saved file: document.pdf 1016540 Bytes.

Complete! Please check your extracted file.
  
user1@linuxbox:~/Desktop$ 

```
![Demo Image2](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/screen.png) 
![Demo Image3](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/screen2.png) 

**Issues:**
* **Reddit -** *While you can upload PNG images via the mobile app, you need to use the Desktop/browser to download them via new.reddit.com.*
* **ImgPile -** *You must sign in to an account before sharing your data-embedded PNG image on ImgPile*.  
		*Sharing your image without logging in, your embedded data will not be preserved.*

 My other programs you may find useful:-
 
* [pdvzip: CLI tool to embed a ZIP file within a tweetable and "executable" PNG-ZIP polyglot image.](https://github.com/CleasbyCode/pdvzip)
* [jdvrif: CLI tool to encrypt & embed any file type within a JPG image.](https://github.com/CleasbyCode/jdvrif)
* [imgprmt: CLI tool to embed an image prompt (e.g. "Midjourney") within a tweetable JPG-HTML polyglot image.](https://github.com/CleasbyCode/imgprmt)
* [jzp: CLI tool to embed small files (e.g. "workflow.json") within a tweetable JPG-ZIP polyglot image.](https://github.com/CleasbyCode/jzp)  
* [pdvps: PowerShell / C++ CLI tool to encrypt & embed any file type within a tweetable & "executable" PNG image](https://github.com/CleasbyCode/pdvps)

##
