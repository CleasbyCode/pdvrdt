# pdvrdt

***pdvrdt*** (*PNG Data Vehicle*) is a fast, easy-to-use steganography command-line tool for concealing and extracting any file type via a **PNG** image. ***Linux only***.

Your data file is compressed with ***libdeflate/zlib***, then encrypted with ***XChaCha20-Poly1305 (libsodium secretstream)*** under a ***256-bit encryption key derived by Argon2id*** from a randomly generated ***128-bit recovery KEY***, and finally embedded in the cover image. The KEY is displayed once, at the end of conceal, as 24 letters and digits in groups of four (*XXXX-XXXX-XXXX-XXXX-XXXX-XXXX*) and is not saved by the program. Without it, the concealed file cannot be recovered. Its last two characters form a checksum, so most typing mistakes are reported straight away.
Images created before ***pdvrdt 6.0*** used a numeric ***recovery PIN*** instead. They remain recoverable with that PIN: ***recover*** reads the image's version where it can and prompts for a ***KEY*** or ***PIN*** to match (*see [Usage](#usage)*).

Using the ***default conceal mode***, you can conceal any file type, with the finished image capped at ***2GiB***. The other platform conceal modes and the compatible social media sites (*listed below*) have their own ***much smaller*** size limits and other requirements.

There is a [***Web edition***](https://cleasbycode.co.uk/pdvrdt/app/) of ***pdvrdt***, which you can use immediately, as a convenient alternative to downloading and compiling the CLI source code. Web file uploads are limited to **20MiB**.

An experimental ***Rust*** port [***pdvrdt-rs***](https://github.com/CleasbyCode/pdvrdt-rs) is also available for those interested in that language. It is format-compatible: either build can recover the other's images.

![Demo Image](https://github.com/CleasbyCode/pdvrdt/blob/main/demo_image/prdt_986700.png)  
***Demo Image -*** ***KEY: V8zh-FPeF-C1cU-rjC1-e5nF-Lazc***

## How pdvrdt conceals data

Unlike the common [***LSB***](https://ctf101.org/forensics/what-is-stegonagraphy/) (*Least Significant Bit*) steganography method of concealing data within the pixels of a cover image, ***pdvrdt*** mostly hides data within the ***chunks*** of a ***PNG*** image (IDAT, iCCP).

| Conceal mode | Where the data goes | Share the output image on |
| --- | --- | --- |
| *(no option)* | IDAT chunk | X-Twitter, ImgPile, PostImage, ImgBB, Flickr |
| ***-m*** | iCCP profile chunk | ***Mastodon*** (also X-Twitter, within its limits) |
| ***-r*** | RGB pixel samples (***adaptive matrix embedding***) | ***Reddit*** only |

The one platform exception to the default chunk storage method is ***Reddit***, which has its own conceal mode. ***Reddit*** re-encodes uploaded images and discards the metadata chunks the default mode relies on, so ***-r*** is the only mode that works there, and it carries the payload in the image's pixels instead.

For the ***Reddit*** conceal mode (***-r***), we use the advanced steganography method [***content-adaptive LSB matching with Hamming-code matrix embedding***](https://www.google.com/search?q=content-adaptive+LSB+matching+with+Hamming+code+matrix+embedding), a spatial-domain [***syndrome coding***](https://www.google.com/search?q=matrix+embedding+syndrome+coding+steganography) scheme, as this is the only storage method that currently works for ***Reddit***. The matrix-embedding half is the construction popularised by [***F5***](https://www.google.com/search?q=F5+steganography+algorithm&sca_esv=4d27f527c866718f&sxsrf=APpeQntqxu1_cdXHpivTEHGFUCf3iPCyEQ%3A1788620777939&udm=50&source=chrome.ob&fbs=ABfTbFVyMZGZf1hfvX9uKjN_-G8c4u0nXx4bEIpwm1lnNH832VTJOOCxW_fyN-Q_ezyf8gKCPb62Sv4Y60wQDsMxJw_GUn1N2yN6o6cIH09xVUI5GF-pNWYoKsCUVySpQOvnQdt_MelXBidhIpc5HOZsc1f3gAPL6dlcX1g-NBn-Ei3lMb4Lvaqg1pMoVwpJyVdX52HDQNTPvlD3Mr-zL1jEdjjuQ6WXCQ&vsint=&aep=1&ntc=1&cs=1&sa=X&ved=2ahUKEwi4iO3Q29eWAxWLh_0HHefkKIYQ2J8OegQIFRAD&biw=1883&bih=992&dpr=1&atvm=2), moved from JPEG coefficients to image pixels.

Each 4 bits of payload are carried by 15 RGB samples drawn from a permutation keyed by the recovery KEY, using ***(1,15,4) Hamming-syndrome matrix embedding***: the syndrome is moved to the target by a single ±1 change (*LSB matching, not LSB replacement*).  

Which sample to change is chosen by a per-sample distortion cost derived from local image activity and channel sensitivity, so edits land in textured regions rather than flat ones. Where two changes cost less than the one required change, it takes them — measured on a 4.3-megapixel cover, 93% of groups need one change, 0.4% take two and 6% need none, about 4.25 bits per changed sample.

Carrier positions are derived from the 128-bit recovery KEY using ***Argon2id*** and ***ChaCha20***, so testing whether an image holds a payload costs a full ***Argon2id*** evaluation per guessed KEY, and recovery reports the same error for a wrong KEY/PIN as for an ordinary PNG. Keyed positions alone do not guarantee that an image is undetectable as steganography.  

***Reddit*** images created with ***pdvrdt 6.0*** or later cannot be recovered by older versions. Images created with ***5.x***, which use a numeric PIN, remain recoverable with that PIN.  

In the default and ***-m*** modes, by contrast, the presence of a payload-carrying chunk is visible to anyone; what is hidden is its contents.

To maximise storage capacity for the ***Reddit*** platform, use a cover image with large dimension sizes, up to the **8192x8192** maximum.  Quality of cover image is not important for this method and should be kept basic for the largest dimensions to help minimise cover image file size.

The ***-r*** mode carries far less data than the default mode, so use ***capsize*** to measure a cover image before choosing a payload (see [Checking capacity](#checking-capacity-with-capsize)).  

https://github.com/user-attachments/assets/5c045cab-515c-4d7a-92ca-4ec79477f929  

https://github.com/user-attachments/assets/ea5420e3-88a3-4dbc-9218-25b93f96575f

https://github.com/user-attachments/assets/8dea1edc-df1f-479e-99d9-37ed4f7dd170

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

Recovery KEY: [***Ad1u-LDhm-KKmU-opwq-FkZp-FCzc***]

Important: Keep your KEY safe, so that you can extract the hidden file.

Complete!

$ pdvrdt recover prdt_221863.png

KEY: ****-****-****-****-****-****

Extracted hidden file: your_secret_file.doc (6165 bytes).

Complete! Please check your file.

```

pdvrdt ***mode*** arguments:

  ***conceal*** - Compresses, encrypts and embeds your secret data file within a ***PNG*** cover image.  
  ***recover*** - Decrypts, uncompresses and extracts the concealed data file from a ***PNG*** cover image (*recovery KEY/PIN required*).  
  ***capsize*** - Reports the adaptive carrier capacity of a cover image for ***-r*** mode. No image is saved.

The ***recover*** prompt says which secret the image takes:

| Image | Prompt |
| --- | --- |
| Default or ***-m*** image made with ***pdvrdt 6.0*** or later | `KEY:` |
| Default or ***-m*** image made before ***pdvrdt 6.0*** | `PIN:` |
| ***-r*** (*Reddit*) image (*any version*) | `KEY/PIN:` |

A ***-r*** image cannot be identified until the KEY or PIN has been entered, because it is what locates the hidden data. Error messages follow the same rule.

Requirements for the cover image:

● PNG only. The cover must be **4MiB** or smaller in the default and ***-m*** modes; ***-r*** instead allows a cover up to **16MiB**.

● Neither side may exceed **4096** pixels in the default and ***-m*** modes. ***-r*** allows up to **8192x8192**, because its capacity comes from the pixels themselves rather than from a chunk appended to the image.

● Animated PNG (***APNG***) covers are rejected. Static PNG colour metadata is preserved.

● ***-m*** mode accepts covers with iCCP or sRGB declarations, but because its payload occupies the iCCP chunk, those declarations are replaced in the output while compatible colour metadata is retained.

● ***-r*** mode emits a non-interlaced, metadata-free, 8-bit RGB PNG, and rejects covers containing transparency.

Requirements for the secret data file:

● The embedded filename must be no longer than **20 bytes**, must not begin with `.` or `-` or end with a space or `.`, and must not contain control characters. The name is stored in the image and restored on ***recover***.

● Your data file is compressed before encryption. Recognised already-compressed file types (`.zip`, `.7z`, `.mp4`, `.jpg`, `.png`, etc) are stored in a level-0 zlib stream instead. For anything else destined for a small platform limit, consider compressing it yourself first (*zip, rar, 7z, etc.*) so that you know its exact stored size.

## Compatible Platforms

*Posting size limit measured by the ***combined*** size of the ***cover image*** + ***compressed data file:****

● ***Flickr*** (**200MB**), ***ImgBB*** (**32MB**), ***PostImage*** (**32MB**), ***ImgPile*** (**8MB**), ***X-Twitter*** (**5MB** + **dimension limits, see below*).

The source file may be larger than a platform limit: ***pdvrdt*** applies the limit to the resulting compressed/encrypted representation, so a highly compressible input remains eligible when the final PNG fits.

*X-Twitter image dimension size limits:*

● ***PNG-32/24*** (*Truecolor*) **68x68** Min. — **900x900** Max.

● ***PNG-8*** (*Indexed-color*) **68x68** Min. — **4096x4096** Max.

**Other: platforms with their own conceal mode:**

● ***Mastodon*** (***-m option***). The finished "*file-embedded*" ***PNG*** must not exceed **16MiB**, so the cover image (itself capped at **4MiB** and **4096x4096**) and the compressed data file share one budget. Because the ***-m*** payload lives in a small chunk rather than the pixels, these images also remain postable on ***X-Twitter*** when they fit its own limits (**10KiB** iCCP chunk and dimensions requirements).

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
Conservative maximum stored payload capacity with a 20-byte filename:  255762 bytes (~249KiB).
Recommended  maximum stored payload capacity with a 20-byte filename:  254738 bytes (~248KiB).
```

The figure reported is the total encrypted ***envelope*** capacity, not a raw secret-file limit: for a payload contained in one encryption frame, the filename, encryption and recovery metadata consume 199 to 218 bytes, and larger payloads add framing overhead. Don't aim at the theoretical limit — where capacity allows, keep the stored payload at least **1KiB** below the conservative maximum. The size check performed by ***conceal*** is the authoritative one.

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

  When saving/downloading an image from ***Reddit*** or ***X-Twitter*** make sure to click on the image within the post to fully expand it before saving.

## Recovery KEY format

This section is for anyone writing a compatible implementation. Everything below is fixed by pdvrdt 6.0.

***The KEY*** is 16 bytes from a cryptographic random number generator (*never all zero*). It is written as:

1. ***Body*** — the 16 bytes read as one 128-bit big-endian integer, written in Base58 as exactly 22 digits, most significant first, padded on the left with `1` (*the zero digit*). Alphabet (*Bitcoin's*): `123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz`.
2. ***Check*** — two more characters:

   ```
   digest = BLAKE2b-256(ASCII("pdvrdt key check v1") || key_bytes)   (19-byte label, no NUL; unkeyed)
   value  = big_endian_u32(digest[0..4]) mod 3364
   check  = alphabet[value / 58] || alphabet[value mod 58]
   ```
3. ***Display*** — the 24 characters in six groups of four, joined with `-`.

When a KEY is entered, spaces, tabs and `-` are ignored and letters are case-sensitive. A numeric entry of 1–20 digits (*no leading zero, at most 18446744073709551615, never 0*) is an old (*pre-6.0*) PIN.

| Key bytes (hex) | KEY |
| --- | --- |
| `00000000000000000000000000000001` | `1111-1111-1111-1111-1111-12BZ` |
| `000000000000000000000000000001bb` | `1111-1111-1111-1111-1111-8e11` (*check value 0*) |
| `00000000000000000000000000003b26` | `1111-1111-1111-1111-1115-W5zz` (*check value 3363*) |
| `000102030405060708090a0b0c0d0e0f` | `112d-rXXU-ifSr-RnXL-GbXg-8Eyf` |
| `80000000000000000000000000000000` | `GokL-Usho-3eiV-vNYN-d1wg-fywf` |
| `ffffffffffffffffffffffffffffffff` | `YcVf-xkQb-6JRz-qk5k-F2tN-Lvdj` |

***Encryption key*** — Argon2id (*libsodium `crypto_pwhash`, ALG_ARGON2ID13, opslimit 2, memlimit 64 MiB*) with the 16 raw key bytes as the password and the random salt stored in the image (*metadata version KDF3*). Old images (*KDF2*) hash the PIN's decimal digits instead.

***Reddit carrier key (-r, carrier version 4)*** — `D` = Argon2id(password = the 16 key bytes, salt = ASCII `pdvrdtcarrier-v4`, opslimit 2, memlimit 64 MiB, 32-byte output); the carrier key is BLAKE2b-256 of ASCII `pdvrdt carrier v4` keyed with `D`. For key bytes `000102…0f` it is `6eba642bddf8a83f77945ef0d588a818a6810090beb97729990957f6f97e91c8`. That 256-bit key drives the same ChaCha20 sample permutation and whitening as carrier version 3 (*see `reddit_steg.cpp`*), which old PIN images still use. This hides *where* the data is. It does not stop statistical steganalysis from detecting that an image carries hidden data.

## Tests

The test scripts in `src/tests` each take the binary to exercise:

```console
$ bash tests/run_golden_tests.sh --bin ./pdvrdt              # recover pre-built images with known KEYs and old PINs
$ bash tests/run_roundtrip_tests.sh --bin ./pdvrdt           # fresh conceal/recover round-trips
$ bash tests/run_safety_regression_tests.sh --bin ./pdvrdt   # negative-path and boundary checks
$ bash tests/run_image_regression_tests.sh --bin ./pdvrdt    # cover-image handling and PNG output
$ python3 tests/run_terminal_regression_tests.py --bin ./pdvrdt  # terminal / KEY- and PIN-entry behaviour
$ python3 tests/run_recovery_path_tests.py --bin ./pdvrdt        # output-file publishing and recovery paths
```

These three build their own test binaries, so they take no `--bin`:

```console
$ bash tests/run_pin_codec_tests.sh          # recovery KEY format: display, parsing, check characters
$ bash tests/run_carrier_kdf_tests.sh        # carrier key derivation, v4 and v1/v2/v3 compatibility
$ bash tests/run_png_decode_limit_tests.sh   # PNG decode bounds and zlib adapter limits
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
