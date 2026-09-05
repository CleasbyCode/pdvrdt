# pdvrdt

***pdvrdt*** (*PNG Data Vehicle*, **v5.0**) is a fast, easy-to-use steganography command-line tool for concealing and extracting any file type via a **PNG** image. ***Linux only***.

Your data file is compressed with ***libdeflate/zlib***, then encrypted with ***XChaCha20-Poly1305*** (***libsodium*** secretstream) under a key derived by ***Argon2id*** from a randomly generated ***recovery PIN***, and finally embedded in the cover image. The PIN is displayed once, at the end of ***conceal***, and is never stored anywhere: without it the concealed file cannot be recovered.

Using the ***default conceal mode***, you can conceal any file type, with the finished image capped at ***2GiB***. The other platform conceal modes and the compatible social media sites (*listed below*) have their own ***much smaller*** size limits and other requirements.

There is a [***Web edition***](https://cleasbycode.co.uk/pdvrdt/app/) of ***pdvrdt***, which you can use immediately, as a convenient alternative to downloading and compiling the CLI source code. Web file uploads are limited to **20MiB**.

An experimental ***Rust*** port [***pdvrdt-rs***](https://github.com/CleasbyCode/pdvrdt-rs) is also available for those interested in that language. It is format-compatible: either build can recover the other's images.

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/prdt_526501.png)  
*Demo Image: **"Wolf"** / ***PIN: 1816136426548255229****

## How pdvrdt conceals data

Unlike the common [***LSB***](https://ctf101.org/forensics/what-is-stegonagraphy/) (*Least Significant Bit*) steganography method of concealing data within the pixels of a cover image, ***pdvrdt*** mostly hides data within the ***chunks*** of a ***PNG*** image (IDAT, iCCP).

| Conceal mode | Where the data goes | Share the output image on |
| --- | --- | --- |
| *(no option)* | IDAT chunk | X-Twitter, ImgPile, PostImage, ImgBB, Flickr |
| ***-m*** | iCCP profile chunk | ***Mastodon*** (also X-Twitter, within its limits) |
| ***-r*** | RGB pixel samples (***adaptive matrix embedding***) | ***Reddit*** only |

The one platform exception to the default chunk storage method is ***Reddit***, which has its own conceal mode. ***Reddit*** re-encodes uploaded images and discards the metadata chunks the default mode relies on, so ***-r*** is the only mode that works there, and it carries the payload in the image's pixels instead.

For the ***Reddit*** conceal mode (***-r***), we use the advanced steganography method [***content-adaptive LSB matching with Hamming-code matrix embedding***](https://www.google.com/search?q=content-adaptive+LSB+matching+with+Hamming+code+matrix+embedding), a spatial-domain [***syndrome coding***](https://www.google.com/search?q=matrix+embedding+syndrome+coding+steganography) scheme, as this is the only storage method that currently works for ***Reddit***. The matrix-embedding half is the construction popularised by [***F5***](https://www.google.com/search?q=F5+steganography+algorithm&sca_esv=4d27f527c866718f&sxsrf=APpeQntqxu1_cdXHpivTEHGFUCf3iPCyEQ%3A1788620777939&udm=50&source=chrome.ob&fbs=ABfTbFVyMZGZf1hfvX9uKjN_-G8c4u0nXx4bEIpwm1lnNH832VTJOOCxW_fyN-Q_ezyf8gKCPb62Sv4Y60wQDsMxJw_GUn1N2yN6o6cIH09xVUI5GF-pNWYoKsCUVySpQOvnQdt_MelXBidhIpc5HOZsc1f3gAPL6dlcX1g-NBn-Ei3lMb4Lvaqg1pMoVwpJyVdX52HDQNTPvlD3Mr-zL1jEdjjuQ6WXCQ&vsint=&aep=1&ntc=1&cs=1&sa=X&ved=2ahUKEwi4iO3Q29eWAxWLh_0HHefkKIYQ2J8OegQIFRAD&biw=1883&bih=992&dpr=1&atvm=2), moved from JPEG coefficients to image pixels.

Each 4 bits of payload are carried by 15 RGB samples drawn from a permutation keyed by the recovery PIN, using ***(1,15,4) Hamming-syndrome matrix embedding***: the syndrome is moved to the target by a single ±1 change (*LSB matching, not LSB replacement*).  

Which sample to change is chosen by a per-sample distortion cost derived from local image activity and channel sensitivity, so edits land in textured regions rather than flat ones. Where two changes cost less than the one required change, it takes them — measured on a 4.3-megapixel cover, 93% of groups need one change, 0.4% take two and 6% need none, about 4.25 bits per changed sample.

Because the sample positions come from the PIN, an image reveals nothing without it: a wrong PIN and an ordinary PNG are indistinguishable.  

In the default and ***-m*** modes, by contrast, the presence of a payload-carrying chunk is visible to anyone; what is hidden is its contents.

To maximise storage capacity for the ***Reddit*** platform, use a cover image with large dimension sizes, up to the **8192x8192** maximum.  Quality of cover image is not important for this method and should be kept basic for the largest dimensions to help minimise cover image file size.

The ***-r*** mode carries far less data than the default mode, so use ***capsize*** to measure a cover image before choosing a payload (see [Checking capacity](#checking-capacity-with-capsize)).

https://github.com/user-attachments/assets/76732196-815b-45ac-b71d-6e1aca672e25  

https://github.com/user-attachments/assets/7a1557d1-4772-4d3d-94a6-ec58d0977a59  

*Image credit: ***"Red_Dragon / [@ultra_arcane](https://x.com/ultra_arcane)"****

## Requirements & Compilation (Linux)

Building requires **CMake 3.20 or newer**, `flock` from **util-linux**, and either **Ninja** (preferred) or **Make**. The compiler must be **GCC 14 or newer**, or **Clang 18 or newer** paired with a C++23 standard library that implements features such as `std::format` and `std::print`. The native libraries required are **libsodium**, **zlib** and **libdeflate** (*1.8 or newer*). ***LodePNG*** is vendored in `src/lodepng`, so it needs no separate package.

```console
$ sudo apt update
$ sudo apt install g++ cmake ninja-build util-linux libsodium-dev zlib1g-dev libdeflate-dev

$ g++ --version   # confirm the reported version is 14 or newer

$ chmod +x compile_pdvrdt.sh
$ ./compile_pdvrdt.sh

$ sudo cp pdvrdt /usr/bin
```

If your distribution ships GCC 14 as a versioned package, install `g++-14` and build with `CXX=g++-14 ./compile_pdvrdt.sh`.

The wrapper keeps dependency-tracked object files under `src/build/`, so later invocations rebuild only what changed, and it replaces the published `pdvrdt` binary only after a complete, successful build. Set `PDVRDT_JOBS=<count>` to change the parallelism limit (default: CPU count, capped at 8), `PDVRDT_BUILD_DIR=<path>` to use a separate build cache, or `PDVRDT_BUILD_MODE=sanitize` for an ASan/UBSan build.

## Usage

```console
$ pdvrdt

Usage: pdvrdt conceal [-m|-r] <cover_image> <secret_file>
       pdvrdt recover <cover_image>
       pdvrdt capsize <cover_image>
       pdvrdt --info
```

Run `pdvrdt --info` for the full built-in guide to modes, platform options and size limits.

```console
$ pdvrdt conceal your_cover_image.png your_secret_file.doc

Platform compatibility for output image:-

 ✓ Flickr
 ✓ ImgBB
 ✓ PostImage
 ✓ ImgPile
 ✓ X-Twitter

Saved "file-embedded" PNG image: prdt_221863.png (57643 bytes).

Recovery PIN: [***14011155878481853319***]

Important: Keep your PIN safe, so that you can extract the hidden file.

Complete!

$ pdvrdt recover prdt_221863.png

PIN: ********************

Extracted hidden file: your_secret_file.doc (6165 bytes).

Complete! Please check your file.

```

pdvrdt ***mode*** arguments:

  ***conceal*** - Compresses, encrypts and embeds your secret data file within a ***PNG*** cover image.  
  ***recover*** - Decrypts, uncompresses and extracts the concealed data file from a ***PNG*** cover image.  
  ***capsize*** - Reports the adaptive carrier capacity of a cover image for ***-r*** mode. No image is saved.

Requirements for the cover image:

● PNG only. The cover must be **4MiB** or smaller in the default and ***-m*** modes; ***-r*** instead allows a cover up to **16MiB**.

● Neither side may exceed **4096** pixels in the default and ***-m*** modes. ***-r*** allows up to **8192x8192**, because its capacity comes from the pixels themselves rather than from a chunk appended to the image.

● Animated PNG (***APNG***) covers are rejected. Static PNG colour metadata is preserved.

● ***-m*** mode accepts covers with iCCP or sRGB declarations, but because its payload occupies the iCCP chunk, those declarations are replaced in the output while compatible colour metadata is retained.

● ***-r*** mode emits a non-interlaced, metadata-free, 8-bit RGB PNG, and rejects covers containing transparency.

Requirements for the secret data file:

● The embedded filename must be no longer than **20 characters** and must not begin with `.` or `-`. The name is stored in the image and restored on ***recover***.

● Your data file is compressed before encryption. Recognised already-compressed file types (`.zip`, `.7z`, `.mp4`, `.jpg`, `.png`, etc) are stored in a level-0 zlib stream instead. For anything else destined for a small platform limit, consider compressing it yourself first (*zip, rar, 7z, etc.*) so that you know its exact stored size.

## Compatible Platforms

*Posting size limit measured by the ***combined*** size of the ***cover image*** + ***compressed data file:****

● ***Flickr*** (**200MB**), ***ImgBB*** (**32MB**), ***PostImage*** (**32MB**), ***ImgPile*** (**8MB**), ***X-Twitter*** (**5MB** + **dimension limits, see below*).

The source file may be larger than a platform limit: ***pdvrdt*** applies the limit to the resulting compressed/encrypted representation, so a highly compressible input remains eligible when the final PNG fits.

*X-Twitter image dimension size limits:*

● ***PNG-32/24*** (*Truecolor*) **68x68** Min. — **900x900** Max.

● ***PNG-8*** (*Indexed-color*) **68x68** Min. — **4096x4096** Max.

**Other: platforms with their own conceal mode:**

● ***Mastodon*** (***-m option***). The finished "*file-embedded*" ***PNG*** must not exceed **16MiB**, so the cover image (itself capped at **4MiB** and **4096x4096**) and the compressed data file share one budget. Because the ***-m*** payload lives in a small chunk rather than the pixels, these images also remain postable on ***X-Twitter*** when they fit its own limits (**10KiB*** iCCP chunk and dimensions requirements).

● ***Reddit*** (***-r option***). The cover image must be no larger than **16MiB** and **8192x8192** pixels; the data file and the finished image must each stay within Reddit's **20MiB** upload ceiling. The actual carrier capacity of the cover is ***much smaller*** than any of those numbers and depends on its dimension sizes. Use `pdvrdt capsize` to measure it.

For platforms such as ***X-Twitter*** & ***ImgPile***, which have smaller data size limits, you may want to focus on data that compresses well, such as text files, etc.

***ImgPile***: sign in to an account before sharing, otherwise the embedded data will not be preserved.

## Checking capacity with capsize

***capsize*** prepares the cover image exactly as ***conceal -r*** would, then reports how much encrypted payload it can carry. Nothing is written to disk. The figures apply only to the ***Reddit*** carrier; the default and ***-m*** modes are limited by output size, not by pixel capacity.

```console
$ pdvrdt capsize basic_img_large_dims.png

Reddit capacity check for conceal -r mode only.

Cover Image: 5400KiB, 1600x1600, Non-interlaced 8-bit RGB PNG, Adaptive (1,15,4) matrix embedding.

Theoretical adaptive capacity limit for this cover image:              255980 bytes (~249KiB).
Conservative maximum compressed capacity with a 20-character filename: 255762 bytes (~249KiB).
Recommended  maximum compressed capacity with a 20-character filename: 254738 bytes (~248KiB).
```

The figure reported is the total encrypted ***envelope*** capacity, not a raw secret-file limit: for a payload contained in one encryption frame, the filename, encryption and recovery metadata consume 199 to 218 bytes, and larger payloads add framing overhead. Don't aim at the theoretical limit — where capacity allows, keep the compressed payload at least **1KiB** below the conservative maximum. The size check performed by ***conceal*** is the authoritative one.

## Conceal mode platform options

To create compatible "*file-embedded*" ***PNG*** images for posting on the ***Mastodon*** platform, you must use the ***-m*** option with ***conceal*** mode.
  ```console
  $ pdvrdt conceal -m my_image.png hidden.doc
```

To create compatible "*data-concealed*" ***PNG*** images for posting on the ***Reddit*** platform, you must use the ***-r*** option with ***conceal*** mode.
  ```console
  $ pdvrdt conceal -r my_image.png hidden.doc
```

  These images are only compatible for posting on ***Reddit***. Your embedded data file will be lost if posted on a different platform.

  When saving/downloading an image from ***Reddit*** make sure to click on the image within the post to fully expand it before saving.

  To correctly download images from ***X-Twitter***, also click the image within the post to fully expand it, before saving.

## Tests

The test scripts in `src/tests` each take the binary to exercise:

```console
$ bash tests/run_golden_tests.sh --bin ./pdvrdt              # recover pre-built images with known PINs
$ bash tests/run_roundtrip_tests.sh --bin ./pdvrdt           # fresh conceal/recover round-trips
$ bash tests/run_safety_regression_tests.sh --bin ./pdvrdt   # negative-path and boundary checks
$ bash tests/run_image_regression_tests.sh --bin ./pdvrdt    # cover-image handling and PNG output
$ python3 tests/run_terminal_regression_tests.py --bin ./pdvrdt  # terminal / PIN-entry behaviour
```

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
- **libdeflate**: Optimized DEFLATE/zlib compression library
  - License: MIT (see the upstream [***COPYING***](https://github.com/ebiggers/libdeflate/blob/master/COPYING) file)

##
