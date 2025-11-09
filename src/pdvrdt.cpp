// PNG Data Vehicle (pdvrdt v4.2). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023

// Compile program (Linux):

// $ sudo apt-get install libsodium-dev

// $ chmod +x compile_pdvrdt.sh
// $ ./compile_pdvrdt.sh
	
// $ Compilation successful. Executable 'pdvrdt' created.
// $ sudo cp pdvrdt /usr/bin
// $ pdvrdt

#include "lodepng/lodepng.h"
// Using lodepng. https://github.com/lvandeve/lodepng  (Copyright (c) 2005-2024 Lode Vandevenne).

#ifdef _WIN32	
	#include "windows/zlib-1.3.1/include/zlib.h"
	
	// zlib.h -- interface of the 'zlib' general purpose compression library
	// version 1.3.1, January 22nd, 2024

	// Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler

	// This software is provided 'as-is', without any express or implied
	// warranty. In no event will the authors be held liable for any damages
	// arising from the use of this software.

	// Permission is granted to anyone to use this software for any purpose,
	// including commercial applications, and to alter it and redistribute it
	// freely, subject to the following restrictions:

	// 1. The origin of this software must not be misrepresented; you must not
	//    claim that you wrote the original software. If you use this software
	//    in a product, an acknowledgment in the product documentation would be
	//    appreciated but is not required.
	// 2. Altered source versions must be plainly marked as such, and must not be
	//    misrepresented as being the original software.
	// 3. This notice may not be removed or altered from any source distribution.

	// Jean-loup Gailly        Mark Adler
	// jloup@gzip.org          madler@alumni.caltech.edu
	
	#define SODIUM_STATIC
	#include "windows/libsodium/include/sodium.h"
	
	// This project uses libsodium (https://libsodium.org/) for cryptographic functions.
	// Copyright (c) 2013-2025 Frank Denis <github@pureftpd.org>
	
    #define NOMINMAX
	#include <windows.h>
	
	#include <conio.h>
#else
	#include <zlib.h>
	#include <sodium.h>
	#include <termios.h>
	#include <unistd.h>
	#include <sys/types.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream> 
#include <iostream>
#include <limits>
#include <iterator> 
#include <initializer_list>
#include <optional>
#include <random> 
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>    

namespace fs = std::filesystem;

static void displayInfo() {
	std::cout << R"(

PNG Data Vehicle (pdvrdt v4.2)
Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

pdvrdt is a metadata “steganography-like” command-line tool used for concealing and extracting
any file type within and from a PNG image.

──────────────────────────
Compile & run (Linux)
──────────────────────────

  $ sudo apt-get install libsodium-dev

  $ chmod +x compile_pdvrdt.sh
  $ ./compile_pdvrdt.sh

  Compilation successful. Executable 'pdvrdt' created.

  $ sudo cp pdvrdt /usr/bin
  $ pdvrdt

──────────────────────────
Usage
──────────────────────────

  pdvrdt conceal [-m|-r] <cover_image> <secret_file>
  pdvrdt recover <cover_image>
  pdvrdt --info

──────────────────────────
Platform compatibility & size limits
──────────────────────────

Share your “file-embedded” PNG image on the following compatible sites.

Size limit is measured by the combined size of cover image + compressed data file:

	• Flickr    (200 MB)
	• ImgBB     (32 MB)
	• PostImage (32 MB)
	• Reddit    (19 MB) — (use -r option).
	• Mastodon  (16 MB) — (use -m option).
	• ImgPile   (8 MB)
	• X-Twitter (5 MB)  — (*Dimension size limits).
	
X-Twitter Image Dimension Size Limits:	

	• PNG-32/24 (Truecolor) 68x68 Min. <-> 900x900 Max.
	• PNG-8 (Indexed-color) 68x68 Min. <-> 4096x4096 Max.

──────────────────────────
Modes
──────────────────────────

  conceal - Compresses, encrypts and embeds your secret data file within a PNG cover image.
  recover - Decrypts, uncompresses and extracts the concealed data file from a PNG cover image
            (recovery PIN required).

──────────────────────────
Platform options for conceal mode
──────────────────────────

  -m (Mastodon) : Createa compatible “file-embedded” PNG images for posting on Mastodon.

      $ pdvrdt conceal -m my_image.png hidden.doc
 
  -r (Reddit) : Creates compatible “file-embedded” PNG images for posting on Reddit.

      $ pdvrdt conceal -r my_image.png secret.mp3

    From the Reddit site, click “Create Post”, then select the “Images & Video” tab to attach the PNG image.
    These images are only compatible for posting on Reddit.

──────────────────────────
Notes
──────────────────────────

• To correctly download images from X-Twitter or Reddit, click image within the post to fully expand it before saving.
• ImgPile: sign in to an account before sharing; otherwise, the embedded data will not be preserved.

)"; 
}

enum class Mode 	: unsigned char { conceal, recover };
enum class Option 	: unsigned char { None, Mastodon, Reddit };

struct ProgramArgs {
	Mode mode{Mode::conceal};
	Option option{Option::None};

	fs::path image_file_path;
	fs::path data_file_path;
    
	static std::optional<ProgramArgs> parse(int argc, char** argv) {
		using std::string_view;

        auto arg = [&](int i) -> string_view {
			return (i >= 0 && i < argc) ? string_view(argv[i]) : string_view{};
        };

        const std::string
        	PROG = fs::path(argv[0]).filename().string(),
       		USAGE = "Usage: " + PROG + " conceal [-m|-r] <cover_image> <secret_file>\n\t\b"
            		+ PROG + " recover <cover_image>\n\t\b"
            		+ PROG + " --info";

        auto die = [&]() -> void {
        	throw std::runtime_error(USAGE);
        };

        if (argc < 2) die();

        if (argc == 2 && arg(1) == "--info") {
        	displayInfo();
        	return std::nullopt;
        }

        ProgramArgs out{};

        const string_view cmd = arg(1);

        if (cmd == "conceal") {
        	int i = 2;

            if (arg(i) == "-m" || arg(i) == "-r") {
        		out.option = (arg(i) == "-m") ? Option::Mastodon : Option::Reddit;
            	++i;
            }

            if (i + 1 >= argc || (i + 2) != argc) die();

            out.image_file_path = fs::path(arg(i));
            out.data_file_path  = fs::path(arg(i + 1));
            out.mode = Mode::conceal;
            return out;
        }

        if (cmd == "recover") {
        	if (argc != 3) die();
        	out.image_file_path = fs::path(arg(2));
        	out.mode = Mode::recover;
        	return out;
        }

        die();
        return out; // Keeps compiler happy.
    }
};

static std::optional<size_t>searchSig(const std::vector<uint8_t>& vec, std::span<const uint8_t> sig, size_t start = 0) {
	if (sig.empty() || start >= vec.size()) return std::nullopt;

    	auto 
    		first = vec.begin() + start,
    		it = std::search(first, vec.end(), sig.begin(), sig.end());
    		
    	if (it == vec.end()) return std::nullopt;
    	return static_cast<size_t>(std::distance(vec.begin(), it));
}

// Writes updated values, such as chunk lengths, crc32, etc. into the relevant vector index location.
static void updateValue(std::vector<uint8_t>& vec, size_t insert_index, uint64_t NEW_VALUE) {
	uint8_t bits = (NEW_VALUE > 0xFFFFFFFFull) ? 64 : 32;
	while (bits) {
		vec[insert_index++] = (NEW_VALUE >> (bits -= 8)) & 0xFF; // Big-endian.
    }	
}

static uint64_t getValue(const std::vector<uint8_t>& vec, size_t index, size_t bytes) {
    if (bytes > 8 || 2 > bytes) {
        throw std::out_of_range("getValue: Invalid bytes value. 2, 4 or 8 only.");
    }
    	
    if (index > vec.size() || vec.size() - index < bytes) {
        throw std::out_of_range("getValue: Index out of bounds");
    }
    	
    size_t value = 0;
    	
    for (size_t i = 0; i < bytes; ++i) {
        value = (value << 8) | static_cast<size_t>(vec[index + i]);
    }
    	
    return value; 
}

static void imageColorCheck(std::vector<uint8_t>& png, bool& hasTwitterBadDims) {
	constexpr uint8_t 
		TRUECOLOR_RGB	 = 2,
		TRUECOLOR_RGBA	 = 6,
		PNG_IEND_BYTES	 = 12,
		TWITTER_MIN_DIMS = 68;
	
	constexpr uint16_t
		TWITTER_MAX_PLTE_DIMS = 4096,  
		TWITTER_MAX_RGB_DIMS  = 900,	
		MIN_RGB_COLORS	      = 257;
		
	std::vector<uint8_t> image; 
	
    lodepng::State state;
    state.decoder.zlibsettings.ignore_adler32 = 1;	
    	
    unsigned 
    	width = 0,
    	height = 0,
    	error = lodepng::decode(image, width, height, state, png);
    	
    if (error) {
    	throw std::runtime_error(std::string("LodePNG Error: ") + std::to_string(error) + ": " + std::string(lodepng_error_text(error)));
    }
    	
    LodePNGColorStats stats;
    lodepng_color_stats_init(&stats);
    stats.allow_palette = 1; 
    stats.allow_greyscale = 0;

    error = lodepng_compute_color_stats(&stats, image.data(), width, height, &state.info_raw);
    if (error) {
        throw std::runtime_error("Lodepng stats error: " + std::to_string(error));
    }
    	
    hasTwitterBadDims = (height > TWITTER_MAX_RGB_DIMS || width > TWITTER_MAX_RGB_DIMS || TWITTER_MIN_DIMS > height || TWITTER_MIN_DIMS > width);
    			
    uint8_t color_type = static_cast<uint8_t>(state.info_png.color.colortype);
    			
 	if ((color_type == TRUECOLOR_RGB || color_type == TRUECOLOR_RGBA) && (MIN_RGB_COLORS > stats.numcolors)) {
 		// This section will re-encode Truecolor images with less than 257 colors to PNG Indexed color type (3).
 		// As part of the re-encode process, it automatically removes any trailing data and superfluous chunks.
 		
    	std::vector<uint8_t> indexed_image(width * height), palette;
        				
        size_t palette_size = stats.numcolors > 0 ? stats.numcolors : 1;

        if (stats.numcolors > 0) {
        	for (size_t i = 0; i < palette_size; ++i) {
            	palette.push_back(stats.palette[i * 4]);     
                palette.push_back(stats.palette[i * 4 + 1]); 
                palette.push_back(stats.palette[i * 4 + 2]); 
                palette.push_back(stats.palette[i * 4 + 3]); 
            }   			
        } else {
			uint8_t r = 0, g = 0, b = 0, a = 255; 
            if (!image.empty()) {
				r = image[0];
                g = image[1];
                b = image[2];
                if (state.info_raw.colortype == LCT_RGBA) a = image[3];
            }
            palette.push_back(r);
            palette.push_back(g);
            palette.push_back(b);
            palette.push_back(a);
        }	
        size_t channels = (state.info_raw.colortype == LCT_RGBA) ? 4 : 3;
        				
        for (size_t i = 0; i < width * height; ++i) {
			uint8_t 
            	r = image[i * channels],
            	g = image[i * channels + 1],
            	b = image[i * channels + 2],
            	a = (channels == 4) ? image[i * channels + 3] : 255;

            size_t index = 0;
            for (size_t j = 0; j < palette_size; ++j) {
                if (palette[j * 4] == r && palette[j * 4 + 1] == g && palette[j * 4 + 2] == b && palette[j * 4 + 3] == a) {
                    index = j;
                    break;
                }
            }	
            indexed_image[i] = static_cast<uint8_t>(index);
        }

        lodepng::State encodeState;
		
        encodeState.info_raw.colortype = LCT_PALETTE; 
        encodeState.info_raw.bitdepth = 8; 
        encodeState.info_png.color.colortype = LCT_PALETTE;
        encodeState.info_png.color.bitdepth = 8;
        encodeState.encoder.auto_convert = 0; 

        for (size_t i = 0; i < palette_size; ++i) {
        	lodepng_palette_add(&encodeState.info_png.color, palette[i * 4], palette[i * 4 + 1], palette[i * 4 + 2], palette[i * 4 + 3]);
        	lodepng_palette_add(&encodeState.info_raw, palette[i * 4], palette[i * 4 + 1], palette[i * 4 + 2], palette[i * 4 + 3]);
        }

        std::vector<uint8_t> output;
        				
        error = lodepng::encode(output, indexed_image.data(), width, height, encodeState);
        if (error) {
        	throw std::runtime_error("Lodepng encode error: " + std::to_string(error));
        }

        png.swap(output);
        			
        hasTwitterBadDims = (height > TWITTER_MAX_PLTE_DIMS || width > TWITTER_MAX_PLTE_DIMS || TWITTER_MIN_DIMS > height || TWITTER_MIN_DIMS > width);
    } else {
		// Because image was not re-encoded, we need to manually check for and remove trailing data and also remove superfluous chunks.
        	constexpr std::array<uint8_t, 4>
    			IEND_SIG { 0x49, 0x45, 0x4E, 0x44 },
				PLTE_SIG { 0x50, 0x4C, 0x54, 0x45 },
				TRNS_SIG { 0x74, 0x52, 0x4E, 0x53 },
				IDAT_SIG { 0x49, 0x44, 0x41, 0x54 };	
	
		// Check for and remove any trialing data after IEND.		
    	if (auto pos_opt = searchSig(png, IEND_SIG)) {
    		const size_t CHUNK_LENGTH_INDEX = *pos_opt - 4;
    		
    		const uint64_t CHUNK_LENGTH = getValue(png, CHUNK_LENGTH_INDEX, 4);
    		
    		const size_t CHUNK_END = CHUNK_LENGTH_INDEX + 8 + CHUNK_LENGTH + 4; 
    			
    		if (CHUNK_END <= png.size()) {
        		png.erase(png.begin() + static_cast<std::ptrdiff_t>(CHUNK_END), png.end());
    		}
    	}
		
    	// Make a copy of the cover image, only keep required chunks. Swap the copy image vector with the original image vector (png).
		constexpr uint8_t 
    		PNG_FIRST_BYTES  = 33,
			INDEXED_PLTE	 = 3;
								
    	std::vector<uint8_t> copied_png;
    	copied_png.reserve(png.size());     
  
    	std::copy_n(png.begin(), PNG_FIRST_BYTES, std::back_inserter(copied_png));	
	
    	auto copy_chunk_type = [&](auto& chunk_signature) {
			constexpr uint8_t 
				CHUNK_FIELDS_COMBINED_LENGTH 	= 12, // Length of: Size_field + Name_field + CRC_field.
				CHUNK_LENGTH_FIELD_SIZE 		= 4,
				INCREMENT_NEXT_SEARCH_INDEX 	= 5;

			uint64_t chunk_length = 0;
		
        	size_t
        		new_chunk_index 	= 0,
				chunk_length_index 	= 0;
		
        	while (auto chunk_opt = searchSig(png, chunk_signature, new_chunk_index)) {
				new_chunk_index = *chunk_opt;
				if (png[new_chunk_index + 4] == 0x78 && png[new_chunk_index + 5] == 0x5E && png[new_chunk_index + 6] == 0x5C) break; // Skip pdvrdt IDAT chunks.
				chunk_length_index = new_chunk_index - CHUNK_LENGTH_FIELD_SIZE;
				chunk_length = getValue(png, chunk_length_index, CHUNK_LENGTH_FIELD_SIZE) + CHUNK_FIELDS_COMBINED_LENGTH;
	    		std::copy_n(png.begin() + chunk_length_index, chunk_length, std::back_inserter(copied_png));
	    		new_chunk_index = new_chunk_index + INCREMENT_NEXT_SEARCH_INDEX;
        	}
    	};
		
		if (color_type == INDEXED_PLTE || color_type == TRUECOLOR_RGB) {
    		if (color_type == INDEXED_PLTE) {
    			copy_chunk_type(PLTE_SIG); 
    		}	
    		copy_chunk_type(TRNS_SIG);
		}
	
    	copy_chunk_type(IDAT_SIG);

    	std::copy_n(png.end() - PNG_IEND_BYTES, PNG_IEND_BYTES, std::back_inserter(copied_png));
    	
    	png.swap(copied_png);	
    }
}

static const uint64_t encryptDataFile(std::vector<uint8_t>& profile, std::vector<uint8_t>& data, std::string& data_filename, bool hasMastodonOption) {
	constexpr uint8_t BYTE_SIZE = 8;

    const size_t 
    	DATA_FILENAME_XOR_KEY_INDEX = hasMastodonOption ? 0x1A6 : 0x15,
    	DATA_FILENAME_INDEX         = hasMastodonOption ? 0x192 : 0x01,
    	SODIUM_KEY_INDEX 	    	= hasMastodonOption ? 0x1BE : 0x2D, // 32 bytes
    	NONCE_KEY_INDEX  	    	= hasMastodonOption ? 0x1DE : 0x4D; // 24 bytes 

    uint8_t data_filename_length = profile[DATA_FILENAME_INDEX - 1];
    
    randombytes_buf(profile.data() + DATA_FILENAME_XOR_KEY_INDEX, data_filename_length);

    std::transform(data_filename.begin(), data_filename.begin() + data_filename_length, profile.begin() + DATA_FILENAME_XOR_KEY_INDEX, profile.begin() + DATA_FILENAME_INDEX, [](char a, uint8_t b){ return static_cast<uint8_t>(a) ^ b; });

    const size_t DATA_SIZE = data.size();

    std::array<uint8_t, crypto_secretbox_KEYBYTES>   key{};
    std::array<uint8_t, crypto_secretbox_NONCEBYTES> nonce{};
    
    crypto_secretbox_keygen(key.data());
    randombytes_buf(nonce.data(), nonce.size());

    std::copy_n(key.begin(),   crypto_secretbox_KEYBYTES,   profile.begin() + SODIUM_KEY_INDEX);
    std::copy_n(nonce.begin(), crypto_secretbox_NONCEBYTES, profile.begin() + NONCE_KEY_INDEX);

    std::vector<uint8_t> encrypted(DATA_SIZE + crypto_secretbox_MACBYTES);
	
    if (crypto_secretbox_easy(encrypted.data(), data.data(), DATA_SIZE, nonce.data(), key.data()) != 0) {
        sodium_memzero(key.data(),   key.size());
        sodium_memzero(nonce.data(), nonce.size());
        throw std::runtime_error("crypto_secretbox_easy failed");
    }

    std::vector<uint8_t>().swap(data);

    profile.insert(profile.end(), encrypted.begin(), encrypted.end());
    std::vector<uint8_t>().swap(encrypted);

    const uint64_t PIN = getValue(profile, SODIUM_KEY_INDEX, BYTE_SIZE);

    size_t
    	sodium_xor_key_pos = SODIUM_KEY_INDEX,
    	sodium_key_pos     = SODIUM_KEY_INDEX;

    uint8_t sodium_keys_length = 48;
    	
    sodium_key_pos += 8; 

    constexpr uint8_t SODIUM_XOR_KEY_LENGTH = 8;

    while (sodium_keys_length--) {
		profile[sodium_key_pos] ^= profile[sodium_xor_key_pos++];
        sodium_key_pos++;
        if (sodium_xor_key_pos >= SODIUM_KEY_INDEX + SODIUM_XOR_KEY_LENGTH) {
        	sodium_xor_key_pos = SODIUM_KEY_INDEX;
        }
    }

    uint64_t random_val;
    randombytes_buf(&random_val, sizeof random_val);
    
    updateValue(profile, SODIUM_KEY_INDEX, random_val);

    sodium_memzero(key.data(),   key.size());
    sodium_memzero(nonce.data(), nonce.size());

    return PIN;
}

static const uint64_t getPin() {
	const std::string MAX_UINT64_STR = "18446744073709551615";
	
	uint64_t pin = 0;
	
	std::cout << "\nPIN: ";
    std::string input;
    char ch; 
    bool sync_status = std::cout.sync_with_stdio(false);
	
	#ifdef _WIN32
		while (input.length() < 20) { 
	 		ch = _getch();
        	if (ch >= '0' && ch <= '9') {
            	input.push_back(ch);
            	std::cout << '*' << std::flush;  
        	} else if (ch == '\b' && !input.empty()) {  
            	std::cout << "\b \b" << std::flush;  
            	input.pop_back();
        	} else if (ch == '\r') {
            	break;
        	}
    	}
	#else
		struct termios oldt, newt;
    	tcgetattr(STDIN_FILENO, &oldt);
    	newt = oldt;
    	newt.c_lflag &= ~(ICANON | ECHO);
    	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	
   		while (input.length() < 20) {
        	ssize_t bytes_read = read(STDIN_FILENO, &ch, 1); 
        	if (bytes_read <= 0) continue; 
       
        	if (ch >= '0' && ch <= '9') {
            	input.push_back(ch);
            	std::cout << '*' << std::flush; 
        	} else if ((ch == '\b' || ch == 127) && !input.empty()) {  
            	std::cout << "\b \b" << std::flush;
            	input.pop_back();
        	} else if (ch == '\n') {
            	break;
        	}
    	}
    	tcsetattr(STDIN_FILENO, TCSANOW, &oldt); 
	#endif

    std::cout << std::endl; 
    std::cout.sync_with_stdio(sync_status);
		
    if (input.empty() || (input.length() == 20 && input > MAX_UINT64_STR)) {
        return pin = 0; 
    } else {
        return pin = std::stoull(input); 
    }
}

// Return calculated crc32 value for PNG chunks.
static uint32_t crcUpdate(uint8_t* buf, uint32_t buf_length) {
	constexpr std::array<uint32_t, 256> CRC_TABLE = {
	    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
	    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
		0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
		0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
		0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
		0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
		0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
		0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
		0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
		0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
		0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
		0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
		0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
		0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
		0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
		0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
		0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
		0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
		0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
		0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
		0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
		0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
		0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
		0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
		0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
		0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
		0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
		0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
		0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
		0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
		0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
		0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
    };

    uint32_t
    	buf_index = 0,
    	crc_value = 0xFFFFFFFF;

    while (buf_length--) {
    	crc_value = CRC_TABLE[(crc_value ^ buf[buf_index++]) & 0xFF] ^ (crc_value >> 8);
    }
    return crc_value ^ 0xFFFFFFFF;
}

static bool hasValidFilename(const fs::path& p) {
	if (p.empty()) {
    	return false;
    }
    
    std::string filename = p.filename().string();
    if (filename.empty()) {
    	return false;
    }

    auto validChar = [](unsigned char c) {
    	return std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == '@' || c == '%';
 	};

    return std::all_of(filename.begin(), filename.end(), validChar);
}

static bool hasFileExtension(const fs::path& p, std::initializer_list<const char*> exts) {
	auto e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    for (const char* cand : exts) {
    	std::string c = cand;
        std::transform(c.begin(), c.end(), c.begin(), [](unsigned char x){ return static_cast<char>(std::tolower(x)); });
        if (e == c) return true;
    }
    return false;
}

// Zlib function, deflate or inflate data file within vector.
static void zlibFunc(std::vector<uint8_t>& vec, Mode mode, Option& option, bool& isCompressedFile) {
	constexpr size_t BUFSIZE = 2 * 1024 * 1024;

    std::vector<uint8_t> buffer_vec(BUFSIZE);
    std::vector<uint8_t> tmp_vec;
    tmp_vec.reserve(vec.size() + BUFSIZE);

    z_stream strm{};
    strm.next_in   = vec.data();
    strm.avail_in  = static_cast<uInt>(std::min(vec.size(), static_cast<size_t>(std::numeric_limits<uInt>::max())));

    strm.next_out  = buffer_vec.data();
    strm.avail_out = static_cast<uInt>(BUFSIZE);

    auto append_written_and_reset_out = [&](z_stream& s) {
    	const size_t written = BUFSIZE - s.avail_out;
        if (written > 0) {
        	tmp_vec.insert(tmp_vec.end(), buffer_vec.begin(), buffer_vec.begin() + written);
        }
        s.next_out  = buffer_vec.data();
        s.avail_out = static_cast<uInt>(BUFSIZE);
    };

    if (mode == Mode::conceal) {
		int compression_level = 0;
		
        if (option == Option::Mastodon) {
        	compression_level = Z_DEFAULT_COMPRESSION;
        } else if (isCompressedFile) {
        	compression_level = Z_NO_COMPRESSION;
        } else {
        	compression_level = Z_BEST_COMPRESSION;
        }

        if (deflateInit(&strm, compression_level) != Z_OK) {
            throw std::runtime_error("Zlib Deflate Init Error");
        }

        int 
        	ret = Z_OK,
        	flush = (strm.avail_in == 0) ? Z_FINISH : Z_NO_FLUSH;

        while (true) {
        	ret = deflate(&strm, flush);
            if (ret == Z_STREAM_ERROR) {
                deflateEnd(&strm);
                throw std::runtime_error("Zlib Compression Error");
            }

            if (strm.avail_out == 0) {
                append_written_and_reset_out(strm);
            }

            if (strm.avail_in == 0 && flush == Z_NO_FLUSH) {
                flush = Z_FINISH;
            }

            if (ret == Z_STREAM_END) {
                append_written_and_reset_out(strm);
                break;
            }
        }

        deflateEnd(&strm);
    } else {
		if (inflateInit(&strm) != Z_OK) {
        	throw std::runtime_error("Zlib Inflate Init Error");
    	}

    	auto spill = [&](z_stream& s) {
        	const size_t written = BUFSIZE - s.avail_out;
        	if (written > 0) {
            	tmp_vec.insert(tmp_vec.end(), buffer_vec.begin(), buffer_vec.begin() + written);
        	}
        	s.next_out  = buffer_vec.data();
        	s.avail_out = static_cast<uInt>(BUFSIZE);
    	};
    
    	while (strm.avail_in > 0) {
        	int ret = inflate(&strm, Z_NO_FLUSH);

        	if (ret == Z_STREAM_END) {
            	spill(strm);
            	inflateEnd(&strm);
            	vec.swap(tmp_vec);
            	return;
        	}

        	if (ret != Z_OK) {
            	inflateEnd(&strm);
            	throw std::runtime_error(std::string("Zlib Inflate Error: ") + (strm.msg ? strm.msg : "unknown"));
        	}

        	if (strm.avail_out == 0) {
            	spill(strm);
        	}
    	}
    	while (true) {
        	int ret = inflate(&strm, Z_FINISH);

        	spill(strm);  

        	if (ret == Z_STREAM_END) {
            	break;     
        	}
        
        	if (ret == Z_OK) {
            	continue;
        	}
        
        	if (ret == Z_BUF_ERROR) {
            	break;
        	}
        
        	if (ret == Z_NEED_DICT || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            	inflateEnd(&strm);
            	throw std::runtime_error(std::string("Zlib Inflate Error during finish: ") + (strm.msg ? strm.msg : "unknown"));
        	}   
        
        	inflateEnd(&strm);
        	throw std::runtime_error("Zlib Inflate: unexpected return code");
    	}	
    	inflateEnd(&strm);
	}
	vec.swap(tmp_vec);
}

static std::string decryptDataFile(std::vector<uint8_t>& png, bool isMastodonFile, bool& hasDecryptionFailed) {
	hasDecryptionFailed = false;

	const size_t 
    	SODIUM_KEY_INDEX = isMastodonFile ? 0x1BE : 0x2D,
    	NONCE_KEY_INDEX  = isMastodonFile ? 0x1DE : 0x4D;
    
    uint8_t sodium_keys_length = 48;
    
    const uint64_t PIN = getPin(); 
    
    updateValue(png, SODIUM_KEY_INDEX, PIN);

    size_t
    	sodium_xor_key_pos = SODIUM_KEY_INDEX,
    	sodium_key_pos     = SODIUM_KEY_INDEX + 8;

    constexpr uint8_t SODIUM_XOR_KEY_LENGTH = 8;

    while (sodium_keys_length--) {
    	png[sodium_key_pos] ^= png[sodium_xor_key_pos++];
        sodium_key_pos++;
        if (sodium_xor_key_pos >= SODIUM_KEY_INDEX + SODIUM_XOR_KEY_LENGTH) {
        	sodium_xor_key_pos = SODIUM_KEY_INDEX;
        }
    }

    std::array<uint8_t, crypto_secretbox_KEYBYTES>   key{};
    std::array<uint8_t, crypto_secretbox_NONCEBYTES> nonce{};

    if (png.size() < std::max(SODIUM_KEY_INDEX + key.size(), NONCE_KEY_INDEX  + nonce.size())) {
    	hasDecryptionFailed = true;
        return {};
    }

    std::copy_n(png.begin() + SODIUM_KEY_INDEX, key.size(),   key.data());
    std::copy_n(png.begin() + NONCE_KEY_INDEX,  nonce.size(), nonce.data());

    const size_t 
    	ENCRYPTED_FILENAME_INDEX = isMastodonFile ? 0x192 : 0x01,
    	FILENAME_XOR_KEY_INDEX   = isMastodonFile ? 0x1A6 : 0x15,
    	FILENAME_LENGTH_INDEX    = ENCRYPTED_FILENAME_INDEX - 1;

    if (png.size() < ENCRYPTED_FILENAME_INDEX) {
        hasDecryptionFailed = true;
        sodium_memzero(key.data(),   key.size());
        sodium_memzero(nonce.data(), nonce.size());
        return {};
    }

    const uint8_t FILENAME_LENGTH = png[FILENAME_LENGTH_INDEX];
    
    if (png.size() < ENCRYPTED_FILENAME_INDEX + FILENAME_LENGTH || png.size() < FILENAME_XOR_KEY_INDEX + FILENAME_LENGTH) {
        hasDecryptionFailed = true;
        sodium_memzero(key.data(),   key.size());
        sodium_memzero(nonce.data(), nonce.size());
        return {};
    }

    std::string decrypted_filename;
    decrypted_filename.resize(FILENAME_LENGTH);

    for (size_t i = 0; i < FILENAME_LENGTH; ++i) {
        decrypted_filename[i] = static_cast<char>(png[ENCRYPTED_FILENAME_INDEX + i] ^ png[FILENAME_XOR_KEY_INDEX + i]);
    }

    const size_t ENCRYPTED_FILE_START_INDEX = isMastodonFile ? 0x1FE : 0x6E;

    if (png.size() < ENCRYPTED_FILE_START_INDEX + crypto_secretbox_MACBYTES) {
        hasDecryptionFailed = true;
        sodium_memzero(key.data(),   key.size());
        sodium_memzero(nonce.data(), nonce.size());
        return {};
    }

    const size_t CIPH_LENGTH = png.size() - ENCRYPTED_FILE_START_INDEX;
    const uint8_t* CIPH    	 = png.data() + ENCRYPTED_FILE_START_INDEX;

    std::vector<uint8_t> decrypted_vec(CIPH_LENGTH - crypto_secretbox_MACBYTES);

    if (crypto_secretbox_open_easy(decrypted_vec.data(), CIPH, CIPH_LENGTH, nonce.data(), key.data()) != 0) {
        std::cerr << "\nDecryption failed!\n";
        hasDecryptionFailed = true;
        sodium_memzero(key.data(),   key.size());
        sodium_memzero(nonce.data(), nonce.size());
        return {};
    }

    png.swap(decrypted_vec);
    
    sodium_memzero(key.data(),   key.size());
    sodium_memzero(nonce.data(), nonce.size());

    return decrypted_filename;
}
	
int main(int argc, char** argv) {
	try {
		#ifdef _WIN32
    		SetConsoleOutputCP(CP_UTF8);  
		#endif
	
		auto args_opt = ProgramArgs::parse(argc, argv);
       	if (!args_opt) return 0; 
       		
		ProgramArgs args = *args_opt; 
			
		bool isCompressedFile = false;
				
		if (!fs::exists(args.image_file_path)) {
        	throw std::runtime_error("Image File Error: File not found.");
    	}
			
		if (!hasValidFilename(args.image_file_path)) {
    		throw std::runtime_error("Invalid Input Error: Unsupported characters in filename arguments.");
		}

		if (!hasFileExtension(args.image_file_path, {".png"})) {
        	throw std::runtime_error("File Type Error: Invalid image extension. Only expecting \".jpg\", \".jpeg\", or \".jfif\".");
    	}
    			
		std::ifstream image_file_ifs(args.image_file_path, std::ios::binary);
        	
    	if (!image_file_ifs) {
    		throw std::runtime_error("Read File Error: Unable to read image file. Check the filename and try again.");
   		}
   		
   		uintmax_t png_size = fs::file_size(args.image_file_path);

    	constexpr uint8_t MINIMUM_IMAGE_SIZE = 87;

    	if (MINIMUM_IMAGE_SIZE > png_size) {
        	throw std::runtime_error("Image File Error: Invalid file size.");
    	}
   		
		constexpr uintmax_t MAX_SIZE_RECOVER = 3ULL * 1024 * 1024 * 1024;    
    	
		if (args.mode == Mode::recover && png_size > MAX_SIZE_RECOVER) {
			throw std::runtime_error("File Size Error: Image file exceeds maximum default size limit for jdvrif.");
		}
		
		std::vector<uint8_t> png_vec(png_size);
	
		image_file_ifs.read(reinterpret_cast<char*>(png_vec.data()), png_size);
		image_file_ifs.close();
		
		constexpr uint32_t LARGE_FILE_SIZE = 300 * 1024 * 1024;
		const std::string LARGE_FILE_MSG = "\nPlease wait. Larger files will take longer to complete this process.\n";
		
		if (args.mode == Mode::conceal) {  
			// --- Embed data file section code.		
			
			std::vector<std::string> platforms_vec { 
				"X-Twitter", "ImgPile", "Mastodon and X-Twitter.", "Mastodon. (Only share this \"file-embedded\" PNG image on Mastodon).", 
				"Reddit. (Only share this \"file-embedded\" PNG image on Reddit).", "PostImage", "ImgBB", "Flickr"
			};
					
			bool 
				hasMastodonOption 	= (args.option == Option::Mastodon),
				hasRedditOption 	= (args.option == Option::Reddit),
				hasNoOption			= (args.option == Option::None),
				hasTwitterBadDims 	= false;
				
			imageColorCheck(png_vec, hasTwitterBadDims);
			                       
        	if (!hasValidFilename(args.data_file_path)) {
				throw std::runtime_error("Invalid Input Error: Unsupported characters in filename arguments.");
    		}
	
			if (!fs::exists(args.data_file_path) || !fs::is_regular_file(args.data_file_path)) {
        		throw std::runtime_error("Data File Error: File not found or not a regular file.");
    		}
    				
    		std::ifstream data_file_ifs(args.data_file_path, std::ios::binary);

			if (!data_file_ifs) {
				throw std::runtime_error("Read File Error: Unable to read data file. Check the filename and try again.");
			}
	
			constexpr uint8_t DATA_FILENAME_MAX_LENGTH = 20;

			std::string data_filename = args.data_file_path.filename().string();

			if (data_filename.size() > DATA_FILENAME_MAX_LENGTH) {
    			throw std::runtime_error("Data File Error: For compatibility requirements, length of data filename must not exceed 20 characters.");
			}
		
			uintmax_t data_size = fs::file_size(args.data_file_path);	
			
    		if (!data_size) {
				throw std::runtime_error("Data File Error: File is empty.");
    		}
    				
    		isCompressedFile = hasFileExtension(args.data_file_path, {
        		".zip",".jar",".rar",".7z",".bz2",".gz",".xz",".tar",".lz",".lz4",".cab",".rpm",".deb",
        		".mp4",".mp3",".exe",".jpg",".jpeg",".jfif",".png",".webp",".bmp",".gif",".ogg",".flac"
    		});
        				
        	constexpr uintmax_t
    			MAX_SIZE_CONCEAL  = 2ULL * 1024 * 1024 * 1024,   
    			MAX_SIZE_REDDIT   = 20ULL * 1024 * 1024,         
   				MAX_SIZE_MASTODON = 16ULL * 1024 * 1024;
	
    		const uintmax_t COMBINED_FILE_SIZE = data_size + png_size;
			
			if (hasMastodonOption && COMBINED_FILE_SIZE > MAX_SIZE_MASTODON) {
				throw std::runtime_error("Data File Size Error: Combined size of image and data file exceeds maximum size limit for the Mastodon platform.");
			}

   			if (hasRedditOption && COMBINED_FILE_SIZE > MAX_SIZE_REDDIT) {
   				throw std::runtime_error("File Size Error: Combined size of image and data file exceeds maximum size limit for the Reddit platform.");
   			}

			if (hasNoOption && COMBINED_FILE_SIZE > MAX_SIZE_CONCEAL) {
				throw std::runtime_error("File Size Error: Combined size of image and data file exceeds maximum default size limit for pdvrdt.");
			}
			
			std::vector<uint8_t> data_vec(data_size);

			data_file_ifs.read(reinterpret_cast<char*>(data_vec.data()), data_size);
			data_file_ifs.close();
			
			// Default vector, where data is stored in the last IDAT chunk of the PNG cover image.
			std::vector<uint8_t>default_vec {
				0x75, 0x5D, 0x19, 0x3D, 0x72, 0xCE, 0x28, 0xA5, 0x60, 0x59, 0x17, 0x98, 0x13, 0x40, 0xB4, 0xDB, 0x3D, 0x18, 0xEC, 0x10, 0xFA, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0x50, 0x3C, 0xEA, 0x5E, 0x9D, 0xF9, 0x90, 0x82
			};
	
			// COLOR PROFILE FOR PNG / Mastodon, where data need to be stored in the iCCP chunk. 0x1FE
			std::vector<uint8_t>mastodon_vec {
				0x00, 0x00, 0x02, 0x98, 0x6C, 0x63, 0x6D, 0x73, 0x02, 0x10, 0x00, 0x00, 0x6D, 0x6E, 0x74, 0x72, 0x52, 0x47, 0x42, 0x20, 0x58, 0x59, 0x5A, 0x20,
				0x07, 0xE2, 0x00, 0x03, 0x00, 0x14, 0x00, 0x09, 0x00, 0x0E, 0x00, 0x1D, 0x61, 0x63, 0x73, 0x70, 0x4D, 0x53, 0x46, 0x54, 0x00, 0x00, 0x00, 0x00,
				0x73, 0x61, 0x77, 0x73, 0x63, 0x74, 0x72, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF6, 0xD6,
				0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0xD3, 0x2D, 0x68, 0x61, 0x6E, 0x64, 0xEB, 0x77, 0x1F, 0x3C, 0xAA, 0x53, 0x51, 0x02, 0xE9, 0x3E, 0x28, 0x6C,
				0x91, 0x46, 0xAE, 0x57, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x64, 0x65, 0x73, 0x63, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x1C,
				0x77, 0x74, 0x70, 0x74, 0x00, 0x00, 0x01, 0x0C, 0x00, 0x00, 0x00, 0x14, 0x72, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x20, 0x00, 0x00, 0x00, 0x14,
				0x67, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x34, 0x00, 0x00, 0x00, 0x14, 0x62, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x48, 0x00, 0x00, 0x00, 0x14,
				0x72, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0x5C, 0x00, 0x00, 0x00, 0x34, 0x67, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0x5C, 0x00, 0x00, 0x00, 0x34,
				0x62, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0x5C, 0x00, 0x00, 0x00, 0x34, 0x63, 0x70, 0x72, 0x74, 0x00, 0x00, 0x01, 0x90, 0x00, 0x00, 0x00, 0x01,
				0x64, 0x65, 0x73, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x6E, 0x52, 0x47, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x54, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x16, 0xC9,
				0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6F, 0xA0, 0x00, 0x00, 0x38, 0xF2, 0x00, 0x00, 0x03, 0x8F, 0x58, 0x59, 0x5A, 0x20,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x62, 0x96, 0x00, 0x00, 0xB7, 0x89, 0x00, 0x00, 0x18, 0xDA, 0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x24, 0xA0, 0x00, 0x00, 0x0F, 0x85, 0x00, 0x00, 0xB6, 0xC4, 0x63, 0x75, 0x72, 0x76, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14,
				0x00, 0x00, 0x01, 0x07, 0x02, 0xB5, 0x05, 0x6B, 0x09, 0x36, 0x0E, 0x50, 0x14, 0xB1, 0x1C, 0x80, 0x25, 0xC8, 0x30, 0xA1, 0x3D, 0x19, 0x4B, 0x40,
				0x5B, 0x27, 0x6C, 0xDB, 0x80, 0x6B, 0x95, 0xE3, 0xAD, 0x50, 0xC6, 0xC2, 0xE2, 0x31, 0xFF, 0xFF, 0x00, 0x12, 0xB7, 0x19, 0x18, 0xA4, 0xEF, 0x15,
				0x8F, 0x9E, 0x7B, 0xB4, 0xF3, 0xAA, 0x0A, 0x5C, 0x80, 0x54, 0xAF, 0xC8, 0x0E, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0x50,
    			0x3C, 0xEA, 0x5E, 0x9D, 0xF9, 0x90
			};
			
			std::vector<uint8_t>profile_vec;
	
			if (hasMastodonOption) {
				profile_vec.swap(mastodon_vec);
				std::vector<uint8_t>().swap(default_vec);
			} else {
				profile_vec.swap(default_vec);
				std::vector<uint8_t>().swap(mastodon_vec);
			}

			const uint16_t PROFILE_NAME_LENGTH_INDEX = hasMastodonOption ? 0x191 : 0x00;
	
			profile_vec[PROFILE_NAME_LENGTH_INDEX] = static_cast<uint8_t>(data_filename.size());

			if (data_size > LARGE_FILE_SIZE) {
				std::cout << LARGE_FILE_MSG;
			}

			// First, compress just the data file. 
			zlibFunc(data_vec, args.mode, args.option, isCompressedFile);

			if (data_vec.empty()) {
				throw std::runtime_error("File Size Error: File is zero bytes. Probable compression failure.");
			}	
			
			const uint64_t PIN = encryptDataFile(profile_vec, data_vec, data_filename, hasMastodonOption);
			
			if (hasRedditOption) {
				constexpr uint8_t 
					CRC_LENGTH = 4,
					INSERT_INDEX_DIFF  = 12;
					 
				constexpr std::array<uint8_t, CRC_LENGTH> IDAT_REDDIT_CRC_BYTES { 0xA3, 0x1A, 0x50, 0xFA };

				std::vector<uint8_t>reddit_vec = { 0x00, 0x08, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54 };
				std::fill_n(std::back_inserter(reddit_vec), 0x80000, 0);

				std::copy_n(IDAT_REDDIT_CRC_BYTES.begin(), CRC_LENGTH, std::back_inserter(reddit_vec));
			
				png_vec.insert(png_vec.end() - INSERT_INDEX_DIFF, reddit_vec.begin(), reddit_vec.end());
				std::vector<uint8_t>().swap(reddit_vec);
				
				platforms_vec[0] = std::move(platforms_vec[4]);
				platforms_vec.resize(1);
			}

			uint32_t mastodon_deflate_size = 0;

			if (hasMastodonOption) {
				// Compresss the data chunk again, this time including the icc color profile. A requirement for iCCP chunk/Mastodon.
				zlibFunc(profile_vec, args.mode, args.option, isCompressedFile);
				mastodon_deflate_size = static_cast<uint32_t>(profile_vec.size());
			}

			constexpr uint8_t 
				DEFAULT_SIZE_DIFF = 3,
				CHUNK_START_INDEX = 4,
				MASTODON_SIZE_DIFF = 9,
				DEFAULT_INDEX_DIFF = 12;
				
			uint8_t chunk_size_index = 0;

			const uint32_t
				PROFILE_VEC_DEFLATE_SIZE = static_cast<uint32_t>(profile_vec.size()),
				IMAGE_VEC_SIZE = static_cast<uint32_t>(png_vec.size()),
				CHUNK_SIZE = hasMastodonOption ? mastodon_deflate_size + MASTODON_SIZE_DIFF : PROFILE_VEC_DEFLATE_SIZE + DEFAULT_SIZE_DIFF,
				CHUNK_INDEX = hasMastodonOption ? 0x21 : IMAGE_VEC_SIZE - DEFAULT_INDEX_DIFF;

			const uint8_t PROFILE_VEC_INDEX = hasMastodonOption ? 0x0D : 0x0B;

			if (hasMastodonOption) {
				constexpr uint8_t MASTODON_ICCP_CHUNK_SIZE_DIFF = 5;
				constexpr uint16_t TWITTER_ICCP_MAX_CHUNK_SIZE 	= 10 * 1024;
				constexpr uint32_t TWITTER_IMAGE_MAX_SIZE 	= 5  * 1024 * 1024;
			
				const uint32_t MASTODON_CHUNK_SIZE = mastodon_deflate_size + MASTODON_ICCP_CHUNK_SIZE_DIFF;

				std::vector<uint8_t>iccp_vec = { 0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
				iccp_vec.reserve(iccp_vec.size() + PROFILE_VEC_DEFLATE_SIZE + CHUNK_SIZE);

				iccp_vec.insert((iccp_vec.begin() + PROFILE_VEC_INDEX), profile_vec.begin(), profile_vec.end());
				updateValue(iccp_vec, chunk_size_index, MASTODON_CHUNK_SIZE);

				const uint32_t ICCP_CHUNK_CRC = crcUpdate(&iccp_vec[CHUNK_START_INDEX], CHUNK_SIZE);
				uint32_t iccp_crc_index = CHUNK_START_INDEX + CHUNK_SIZE;

				updateValue(iccp_vec, iccp_crc_index, ICCP_CHUNK_CRC);
				png_vec.insert((png_vec.begin() + CHUNK_INDEX), iccp_vec.begin(), iccp_vec.end());
			
				std::vector<uint8_t>().swap(iccp_vec);
			
				if (!hasTwitterBadDims && TWITTER_IMAGE_MAX_SIZE >= png_vec.size() && TWITTER_ICCP_MAX_CHUNK_SIZE >= MASTODON_CHUNK_SIZE) {
					platforms_vec[0] = std::move(platforms_vec[2]);
				} else {
					platforms_vec[0] = std::move(platforms_vec[3]);
				}
				platforms_vec.resize(1);
			} else {
				constexpr uint8_t 
					IDAT_CHUNK_SIZE_DIFF = 4,
					IDAT_CHUNK_CRC_INDEX_DIFF = 8;

	     		static std::vector<uint8_t>idat_vec = { 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54, 0x78, 0x5E, 0x5C, 0x00, 0x00, 0x00, 0x00 };
				idat_vec.reserve(idat_vec.size() + profile_vec.size() + CHUNK_SIZE);
		
	     		idat_vec.insert(idat_vec.begin() + PROFILE_VEC_INDEX, profile_vec.begin(), profile_vec.end());		
				updateValue(idat_vec, chunk_size_index, CHUNK_SIZE);

				const uint32_t IDAT_CHUNK_CRC = crcUpdate(&idat_vec[CHUNK_START_INDEX], CHUNK_SIZE + IDAT_CHUNK_SIZE_DIFF);
				uint32_t idat_crc_index = CHUNK_SIZE + IDAT_CHUNK_CRC_INDEX_DIFF;

				updateValue(idat_vec, idat_crc_index, IDAT_CHUNK_CRC);
				png_vec.insert((png_vec.begin() + CHUNK_INDEX), idat_vec.begin(), idat_vec.end());

				std::vector<uint8_t>().swap(idat_vec);
			}
	
			std::vector<uint8_t>().swap(profile_vec);
			
			std::random_device rd;
 			std::mt19937 gen(rd());
    		std::uniform_int_distribution<> dist(10000, 99999);  

			const std::string OUTPUT_FILENAME = "prdt_" + std::to_string(dist(gen)) + ".png";

			std::ofstream file_ofs(OUTPUT_FILENAME, std::ios::binary);

			if (!file_ofs) {
				throw std::runtime_error("Write Error: Unable to write to file.");
			}
	
			const uint32_t OUTPUT_SIZE = static_cast<uint32_t>(png_vec.size());

			file_ofs.write(reinterpret_cast<const char*>(png_vec.data()), OUTPUT_SIZE);
			file_ofs.close();
		
			if (hasNoOption) {
				platforms_vec.erase(platforms_vec.begin() + 2, platforms_vec.begin() + 5);
			
				constexpr uint32_t 
					FLICKR_MAX_IMAGE_SIZE 			= 200 * 1024 * 1024,
					IMGBB_POSTIMAGE_MAX_IMAGE_SIZE 	= 32  * 1024 * 1024,
					IMGPILE_MAX_IMAGE_SIZE 			= 8   * 1024 * 1024,
					TWITTER_MAX_IMAGE_SIZE 			= 5   * 1024 * 1024;
				
				std::vector<std::string> filtered_platforms;

				for (const std::string& platform : platforms_vec) {
    				if ((platform == "X-Twitter" && OUTPUT_SIZE > TWITTER_MAX_IMAGE_SIZE) || (platform == "X-Twitter" && hasTwitterBadDims)) {
        				continue;
    				}
    				if ((platform == "ImgBB" || platform == "PostImage") && (OUTPUT_SIZE > IMGBB_POSTIMAGE_MAX_IMAGE_SIZE)) {
        				continue;
    				}
    				if (platform == "ImgPile" && OUTPUT_SIZE > IMGPILE_MAX_IMAGE_SIZE) {
        				continue;
    				}
    				if (platform == "Flickr" && OUTPUT_SIZE > FLICKR_MAX_IMAGE_SIZE) {
        				continue;
    				}	
					filtered_platforms.push_back(platform);
				}
				
				if (filtered_platforms.empty()) {
    				filtered_platforms.push_back("\b\bUnknown!\n\n Due to the large file size of the output PNG image, I'm unaware of any\n compatible platforms that this image can be posted on. Local use only?");
				}	
				
				platforms_vec.swap(filtered_platforms);
				std::vector<std::string>().swap(filtered_platforms);
			}
			
			std::cout << "\nPlatform compatibility for output image:-\n\n";
			
			for (const auto& s : platforms_vec) {
        		std::cout << " ✓ "<< s << '\n' ;
   		 	}	
   		 	
			std::vector<std::string>().swap(platforms_vec);
			std::vector<uint8_t>().swap(png_vec);
		
			std::cout << "\nSaved \"file-embedded\" PNG image: " << OUTPUT_FILENAME << " (" << OUTPUT_SIZE << " bytes).\n";

			std::cout << "\nRecovery PIN: [***" << PIN << "***]\n\nImportant: Keep your PIN safe, so that you can extract the hidden file.\n\nComplete!\n\n";
					 
			return 0;
		} else {
			// ------- Recover data file section code.	
			constexpr std::array<uint8_t, 7> 
				ICCP_SIG { 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63 },
				PDV_SIG  { 0xC6, 0x50, 0x3C, 0xEA, 0x5E, 0x9D, 0xF9 };
			
			constexpr uint8_t ICCP_CHUNK_INDEX = 0x25;
		
			auto
				iccp_chunk_opt = searchSig(png_vec, ICCP_SIG),
				pdv_chunk_opt  = searchSig(png_vec, PDV_SIG);

			uint32_t iccp_chunk_index = static_cast<uint32_t>(*iccp_chunk_opt);
						
			if (iccp_chunk_index != ICCP_CHUNK_INDEX && (!pdv_chunk_opt)) {
				throw std::runtime_error("Image File Error: This is not a pdvrdt image.");
			}

			bool isMastodonFile = (iccp_chunk_index == ICCP_CHUNK_INDEX && (!pdv_chunk_opt));
		
			constexpr uint8_t 
				DEFAULT_CHUNK_SIZE_INDEX_DIFF 	 	= 112,
				DEFAULT_PROFILE_DATA_INDEX_DIFF  	= 11,
				MASTODON_PROFILE_DATA_INDEX_DIFF 	= 9,
				MASTODON_CHUNK_SIZE_DIFF 	 		= 9,
				MASTODON_CHUNK_SIZE_INDEX_DIFF 	 	= 4,
				BYTE_SIZE							= 4,
				CHUNK_SIZE_DIFF 		 			= 3;

            #if defined(_MSC_VER)
            	__analysis_assume(pdv_chunk_opt.has_value());
            #endif

		 	uint32_t pdv_chunk_index = static_cast<uint32_t>(*pdv_chunk_opt);
	
			const uint32_t
				CHUNK_SIZE_INDEX = isMastodonFile ? iccp_chunk_index - MASTODON_CHUNK_SIZE_INDEX_DIFF : pdv_chunk_index - DEFAULT_CHUNK_SIZE_INDEX_DIFF,				
				PROFILE_DATA_INDEX = isMastodonFile ? iccp_chunk_index + MASTODON_PROFILE_DATA_INDEX_DIFF : CHUNK_SIZE_INDEX + DEFAULT_PROFILE_DATA_INDEX_DIFF;
		
			const uint64_t CHUNK_SIZE = isMastodonFile ? getValue(png_vec, CHUNK_SIZE_INDEX, BYTE_SIZE) - MASTODON_CHUNK_SIZE_DIFF : getValue(png_vec, CHUNK_SIZE_INDEX, BYTE_SIZE);

			uint8_t byte = png_vec.back();

			png_vec.erase(png_vec.begin(), png_vec.begin() + PROFILE_DATA_INDEX);
			png_vec.erase(png_vec.begin() + (isMastodonFile ? CHUNK_SIZE + CHUNK_SIZE_DIFF : CHUNK_SIZE - CHUNK_SIZE_DIFF), png_vec.end());
		
			if (isMastodonFile) {
				zlibFunc(png_vec, args.mode, args.option, isCompressedFile);
				if (png_vec.empty()) {
					throw std::runtime_error("File Size Error: File is zero bytes. Probable failure inflating file.");
				}
				auto pdv_iccp_chunk_opt = searchSig(png_vec, PDV_SIG);
		
				if (!pdv_iccp_chunk_opt) {
					throw std::runtime_error("Image File Error: This is not a pdvrdt image.");
				}
			}

			if (png_size > LARGE_FILE_SIZE) {
				std::cout << LARGE_FILE_MSG;
			}

			bool hasDecryptionFailed = false;
			
			std::string decrypted_filename = decryptDataFile(png_vec, isMastodonFile, hasDecryptionFailed);
			
			if (hasDecryptionFailed) {	
				std::fstream file(args.image_file_path, std::ios::in | std::ios::out | std::ios::binary); 
   		 
	    		file.seekg(-1, std::ios::end);
	    		std::streampos byte_pos = file.tellg();

	    		file.read(reinterpret_cast<char*>(&byte), sizeof(byte));

    	    	if (byte == 0x82) {
    				byte = 0;
				} else {
	   				byte++;
				}		
				if (byte > 2) {
					file.close();
					std::ofstream file(args.image_file_path, std::ios::out | std::ios::trunc | std::ios::binary);
				} else {
					file.seekp(byte_pos);
					file.write(reinterpret_cast<char*>(&byte), sizeof(byte));
				}
				file.close();
    	    	throw std::runtime_error("File Recovery Error: Invalid PIN or file is corrupt.");
			}
		
			zlibFunc(png_vec, args.mode, args.option, isCompressedFile);

			const uint32_t INFLATED_FILE_SIZE = static_cast<uint32_t>(png_vec.size());

			if (!INFLATED_FILE_SIZE) {
				throw std::runtime_error("Zlib Compression Error: Output file is empty. Inflating file failed.");
			}

			if (byte != 0x82) {	
				std::fstream file(args.image_file_path, std::ios::in | std::ios::out | std::ios::binary); 
   		 
				file.seekg(-1, std::ios::end);
				std::streampos byte_pos = file.tellg();

				byte = 0x82;

				file.seekp(byte_pos);
				file.write(reinterpret_cast<char*>(&byte), sizeof(byte));

				file.close();
			}
		
			std::ofstream file_ofs(decrypted_filename, std::ios::binary);

			if (!file_ofs) {
				throw std::runtime_error("Write File Error: Unable to write to file.");
			}

			file_ofs.write(reinterpret_cast<const char*>(png_vec.data()), INFLATED_FILE_SIZE);
			file_ofs.close();

			std::cout << "\nExtracted hidden file: " << decrypted_filename << " (" << INFLATED_FILE_SIZE << " bytes).\n\nComplete! Please check your file.\n\n";

			return 0;
	    }   
	}
	catch (const std::runtime_error& e) {
    	std::cerr << "\n" << e.what() << "\n\n";
        return 1;
    }
}
