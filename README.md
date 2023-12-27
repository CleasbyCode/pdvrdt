# pdvrdt
Use this command-line tool to embed or extract any file type via a **PNG** image.  
You can share your image on several *social media sites, which will retain the embedded data. 

**Image size limits vary across platforms:*

* *Flickr (200MB), ImgBB (32MB), PostImage (24MB), \*~~Reddit (20MB)~~, Imgur (20MB / with -i option)*
* *Mastodon (16MB / requires -m option), ImgPile (8MB), Imgur (5MB), Twitter (5MB).*

*Status update: \*Reddit (currently) no longer working with pdvrdt.*

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/pdv_13421_img.png)  
***{Image credit: [MΞV.ai / @aest_artificial](https://twitter.com/aest_artificial)}***  

Demo Videos: [**Twitter**](https://youtu.be/rVsZxWgmurE) / [**Reddit**](https://youtu.be/p34bii_b8n4)  
 
For **Mastodon** (*-m option required*) your data file is stored within the ***iCCP chunk*** of the PNG image.  
For all the other compatible sites listed above, your data file is stored within an ***IDAT chunk***, of the PNG image.

Your data file is encrypted & compressed.

*Using the -i (imgur) option, increases the Imgur PNG upload size limit from 5MB to 20MB.*

*Once the PNG image has been uploaded to your Imgur page, you can grab links of the image for sharing.*

*If the file-embedded image is over 5MB (when using the -i (imgur) option) I would avoid posting the image to
the "*Imgur Community Page*", as the thumbnail preview fails and shows as a broken icon image.*

*(Clicking the "*broken*" preview image will still take you to the correctly displayed full image).*

*Only use this option for posting file-embedded images over 5MB on Imgur, as the image with the -i (imgur) option
is not compatible with the other listed sites.*

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

You can now post your file-embedded PNG image(s) to the relevant supported platforms.

user1@linuxbox:~/Desktop$ ./pdvrdt

Usage: pdvrdt -e [-i] [-m] [-r] <cover_image> <data_file>  
       pdvrdt -x <file_embedded_image>  
       pdvrdt --info
        
user1@linuxbox:~/Desktop$ ./pdvrdt -x pdv_17627_rdt.png

eXtract mode selected.

Reading embedded PNG image file. Please wait...

Found compressed IDAT chunk.

Inflating IDAT chunk.

Found pdvrdt signature.

Extracting encrypted data file from the IDAT chunk.

Decrypting extracted data file.

Writing decrypted data file out to disk.

Saved file: "document.pdf" Size: "1016540 Bytes"

Complete! Please check your extracted file(s).
  
user1@linuxbox:~/Desktop$ 

```
**Issues:**
* **Reddit -** *Does not work with Reddit's mobile app. Desktop/browser only.*
* **ImgPile -** *You must sign in to an account before sharing your data-embedded PNG image on ImgPile*.  
		*Sharing your image without logging in, your embedded data will not be preserved.*

 My other programs you may find useful:-
 
* [pdvzip: CLI tool to embed a ZIP file within a tweetable and "executable" PNG-ZIP polyglot image.](https://github.com/CleasbyCode/pdvzip)
* [jdvrif: CLI tool to encrypt & embed any file type within a JPG image.](https://github.com/CleasbyCode/jdvrif)
* [imgprmt: CLI tool to embed an image prompt (e.g. "Midjourney") within a tweetable JPG-HTML polyglot image.](https://github.com/CleasbyCode/imgprmt)
* [jzp: CLI tool to embed small files (e.g. "workflow.json") within a tweetable JPG-ZIP polyglot image.](https://github.com/CleasbyCode/jzp)  
* [pdvps: PowerShell / C++ CLI tool to encrypt & embed any file type within a tweetable & "executable" PNG image](https://github.com/CleasbyCode/pdvps)

##
