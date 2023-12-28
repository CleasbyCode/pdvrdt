# pdvrdt
Use this command-line tool to embed or extract any file type via a **PNG** image.  
You can share your image on several *social media sites, which will retain the embedded data. 

**Image size limits vary across platforms:*

* *Flickr (200MB), ImgBB (32MB), PostImage (24MB), \*Reddit (20MB / -r option), Imgur (20MB / -i option)*
* *Mastodon (16MB / -m option), ImgPile (8MB), Imgur (5MB), \*Twitter (5MB / Dimension limits).*

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/pdv_13421_img.png)  
***{Image credit: [MΞV.ai / @aest_artificial](https://twitter.com/aest_artificial)}***  

Demo Videos: [**Twitter**](https://youtu.be/wSkP7LU7woQ) / [**Reddit**](https://youtu.be/7-ZbXv8NqA0) / [**Mastodon**](https://youtu.be/2giS6rP8dvI)

Your embedded data file is encrypted & compressed.

*To post/share file-embedded PNG images on **Reddit**, you need to use the **-r** option.*  
*To post/share file-embedded PNG images on **Mastodon**, you need to use the **-m** option.*  

As well as the 5MB image size limit, **Twitter** also has dimension size limits.  
*PNG-32/24 (Truecolor) 900x900 Max. 68x68 Min.*  
*PNG-8 (Indexed color) 4096x4096 Max. 68x68 Min.*  

*When posting file-embedded images on **Reddit**, always the select the "**Images & Video**" tab.*  
*When saving file-embedded images from **Reddit**, click the image in the post to expand it, then save it.
You should see the filename with a .png extension in the address bar of your browser.*  

*When saving file-embedded images from **Twitter**, always click the image first to fully expand it, before saving.*

*Using the **-i** option, increases the **Imgur** PNG default size limit from 5MB to 20MB.*

*Once the PNG image has been uploaded to your **Imgur** page, you can grab links of the image for sharing.*

*If the file-embedded image is over 5MB (when using the **-i** option), avoid posting the image to
the "*Imgur Community Page*", as the thumbnail preview fails and shows as a broken icon image.*

*(Clicking the "*broken*" preview image will still take you to the correctly displayed full image).*

Compile and run the program under Windows or **Linux**.

## Usage (Linux - Insert file into PNG image / Extract file from image)

```console

user1@linuxbox:~/Desktop$ g++ pdvrdt.cpp -O2 -lz -s -o pdvrdt
user1@linuxbox:~/Desktop$
user1@linuxbox:~/Desktop$ ./pdvrdt 

Usage: pdvrdt -e [-i] [-m] [-r] <cover_image> <data_file>  
       pdvrdt -x <file_embedded_image>  
       pdvrdt --info

user1@linuxbox:~/Desktop$ ./pdvrdt -e rabbit.png document.pdf
  
Embed mode selected.

Reading files. Please wait...

Encrypting data file.

Compressing data file.

Embedding data file within the PNG image.

Writing file-embedded PNG image out to disk.

Created PNG image: prdt_17627.png 1245285 Bytes.

Complete!

You can now post your file-embedded PNG image to the relevant supported platforms.

user1@linuxbox:~/Desktop$ ./pdvrdt

Usage: pdvrdt -e [-i] [-m] [-r] <cover_image> <data_file>  
       pdvrdt -x <file_embedded_image>  
       pdvrdt --info
        
user1@linuxbox:~/Desktop$ ./pdvrdt -x prdt_17627.png

eXtract mode selected.

Reading PNG image file. Please wait...

Found compressed data chunk.

Inflating data.

Found pdvrdt signature.

Extracting encrypted data file from the PNG image.

Decrypting data file.

Writing data file out to disk.

Saved file: document.pdf 1016540 Bytes.

Complete! Please check your extracted file.
  
user1@linuxbox:~/Desktop$ 

```
**Issues:**
* **Reddit -** *Post/share embedded images on Reddit via the Desktop/browser only. Mobile app not supported.*
* **ImgPile -** *You must sign in to an account before sharing your data-embedded PNG image on ImgPile*.  
		*Sharing your image without logging in, your embedded data will not be preserved.*

 My other programs you may find useful:-
 
* [pdvzip: CLI tool to embed a ZIP file within a tweetable and "executable" PNG-ZIP polyglot image.](https://github.com/CleasbyCode/pdvzip)
* [jdvrif: CLI tool to encrypt & embed any file type within a JPG image.](https://github.com/CleasbyCode/jdvrif)
* [imgprmt: CLI tool to embed an image prompt (e.g. "Midjourney") within a tweetable JPG-HTML polyglot image.](https://github.com/CleasbyCode/imgprmt)
* [jzp: CLI tool to embed small files (e.g. "workflow.json") within a tweetable JPG-ZIP polyglot image.](https://github.com/CleasbyCode/jzp)  
* [pdvps: PowerShell / C++ CLI tool to encrypt & embed any file type within a tweetable & "executable" PNG image](https://github.com/CleasbyCode/pdvps)

##
