// PNG Data Vehicle (pdvrdt v3.6). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023

// Compile program (Linux):

// $ sudo apt-get install libsodium-dev

// $ chmod +x compile_pdvrdt.sh
// $ ./compile_pdvrdt.sh
	
// $ Compilation successful. Executable 'pdvrdt' created.
// $ sudo cp pdvrdt /usr/bin
// $ pdvrdt

#include "lodepng/lodepng.h"
// Using lodepng. https://github.com/lvandeve/lodepng  (Copyright (c) 2005-2024 Lode Vandevenne).

#include <zlib.h>

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
#include <sodium.h>

// This project uses libsodium (https://libsodium.org/) for cryptographic functions.
// Copyright (c) 2013-2025 Frank Denis <github@pureftpd.org>

#ifdef _WIN32
	#include <conio.h>
	
	#define NOMINMAX
	#include <windows.h>
#else
	#include <termios.h>
	#include <unistd.h>
#endif

#include "profileVec.h" 
#include "fileChecks.h" 
#include <cstring>
#include <array>
#include <algorithm>
#include <utility>        
#include <cstddef>
#include <cstdlib>
#include <stdexcept>
#include <fstream>          
#include <iostream>         
#include <filesystem>       
#include <random>       
#include <iterator>   

int main(int argc, char** argv) {
	try {	
		#ifdef _WIN32
    			SetConsoleOutputCP(CP_UTF8);  
		#endif
	
		ProgramArgs args = ProgramArgs::parse(argc, argv);
		
		std::vector<uint8_t> 
        		cover_image_vec,
        		data_file_vec;
		
		uintmax_t 
			cover_image_size = 0,
			data_file_size = 0;
			
		bool isCompressedFile = false;
		
		validateImageFile(args.cover_image, args.mode, args.platform, cover_image_size, cover_image_vec);
		
		auto getValue = [](const std::vector<uint8_t>& vec, uint32_t index, uint8_t bytes) -> uint64_t {
    			uint64_t value = 0;
    			for (uint8_t i = 0; i < bytes; ++i) {
        			value = (value << 8) | static_cast<uint64_t>(vec[index + i]);
    			}
    			return value;
		};
		
		auto updateValue = [](std::vector<uint8_t>& vec, uint32_t insert_index, const uint64_t NEW_VALUE, uint8_t bits) {
					while (bits) { vec[insert_index++] = (NEW_VALUE >> (bits -= 8)) & 0xff; }	// Big-endian.
				};

		auto searchSig = []<typename T, size_t N>(std::vector<uint8_t>& vec, uint32_t start_index, const uint8_t INCREMENT_SEARCH_INDEX, const std::array<T, N>& SIG) -> uint32_t {
    					return static_cast<uint32_t>(std::search(vec.begin() + start_index + INCREMENT_SEARCH_INDEX, vec.end(), SIG.begin(), SIG.end()) - vec.begin());
				};
		
		auto zlibFunc = [&platform = args.platform, &isCompressedFile](std::vector<uint8_t>& vec, ArgMode mode) {
			constexpr uint32_t BUFSIZE = 2 * 1024 * 1024; 
			
			const uint32_t VEC_SIZE = static_cast<uint32_t>(vec.size());
			
	    		std::vector<uint8_t> buffer_vec(BUFSIZE);
    			std::vector<uint8_t> tmp_vec;
    			tmp_vec.reserve(VEC_SIZE + BUFSIZE);

    			z_stream strm{};
    			strm.next_in   = vec.data();
    			strm.avail_in  = VEC_SIZE;
    			strm.next_out  = buffer_vec.data();
    			strm.avail_out = BUFSIZE;

    			if (mode == ArgMode::conceal) {
    				auto select_compression_level = [&platform](uint32_t vec_size, bool isCompressedFile) -> int {
    					constexpr uint32_t FIFTH_SIZE_OPTION   = 750 * 1024 * 1024;
    					constexpr uint32_t FOURTH_SIZE_OPTION  = 450 * 1024 * 1024;
    					constexpr uint32_t THIRD_SIZE_OPTION   = 250 * 1024 * 1024;
    					constexpr uint32_t SECOND_SIZE_OPTION  = 150 * 1024 * 1024;
    					constexpr uint32_t FIRST_SIZE_OPTION   = 10 * 1024 * 1024;
    					
    					if (platform == ArgOption::mastodon) return Z_DEFAULT_COMPRESSION;
    					if (isCompressedFile || vec_size >= FIFTH_SIZE_OPTION) return Z_NO_COMPRESSION;
    					if (vec_size >= FOURTH_SIZE_OPTION) return Z_BEST_SPEED;
    					if (vec_size >= THIRD_SIZE_OPTION)  return Z_DEFAULT_COMPRESSION;
    					if (vec_size >= SECOND_SIZE_OPTION) return Z_BEST_COMPRESSION;
    					if (vec_size >= FIRST_SIZE_OPTION)  return Z_DEFAULT_COMPRESSION;
    					return Z_BEST_COMPRESSION;
				};
        
        			int compression_level = select_compression_level(VEC_SIZE, isCompressedFile);

        			if (deflateInit(&strm, compression_level)  != Z_OK) {
            				throw std::runtime_error("Zlib Deflate Init Error");
        			}
        			while (strm.avail_in > 0) {
            				int ret = deflate(&strm, Z_NO_FLUSH);
            				if (ret != Z_OK) {
                				deflateEnd(&strm);
                				throw std::runtime_error("Zlib Compression Error");
            				}
            				if (strm.avail_out == 0) {
                				tmp_vec.insert(tmp_vec.end(), buffer_vec.begin(), buffer_vec.end());
                				strm.next_out = buffer_vec.data();
                				strm.avail_out = BUFSIZE;
            				}
        			}
        			int ret;
        			do {
            				ret = deflate(&strm, Z_FINISH);
            				size_t bytes_written = BUFSIZE - strm.avail_out;
            				if (bytes_written > 0) {
                				tmp_vec.insert(tmp_vec.end(),
                               			buffer_vec.begin(),
                               			buffer_vec.begin() + bytes_written);
            				}
            				strm.next_out = buffer_vec.data();
            				strm.avail_out = BUFSIZE;
        			} while (ret == Z_OK);
        			deflateEnd(&strm);
    			} else { // (inflate)
        			if (inflateInit(&strm) != Z_OK) {
            				throw std::runtime_error("Zlib Inflate Init Error");
        			}
        			while (strm.avail_in > 0) {
            				int ret = inflate(&strm, Z_NO_FLUSH);
            				if (ret == Z_STREAM_END) {
                				size_t bytes_written = BUFSIZE - strm.avail_out;
                				if (bytes_written > 0) {
                    					tmp_vec.insert(tmp_vec.end(),
                                   			buffer_vec.begin(),
                                   			buffer_vec.begin() + bytes_written);
                				}
                				inflateEnd(&strm);
                				goto inflate_done; 
            				}
            				if (ret != Z_OK) {
                				inflateEnd(&strm);
                				throw std::runtime_error(
                    				"Zlib Inflate Error: " +
                    				std::string(strm.msg ? strm.msg : "Unknown error"));
            				}
            				if (strm.avail_out == 0) {
                				tmp_vec.insert(tmp_vec.end(), buffer_vec.begin(), buffer_vec.end());
                				strm.next_out = buffer_vec.data();
                				strm.avail_out = BUFSIZE;
            				}
        			}

        			{	
            				int ret;
            				do {
                				ret = inflate(&strm, Z_FINISH);
                				size_t bytes_written = BUFSIZE - strm.avail_out;
                				if (bytes_written > 0) {
                    					tmp_vec.insert(tmp_vec.end(),
                                	  	 	buffer_vec.begin(),
                                	   		buffer_vec.begin() + bytes_written);
                				}
                				strm.next_out = buffer_vec.data();
                				strm.avail_out = BUFSIZE;
            				} while (ret == Z_OK);
        			}
        			inflateEnd(&strm);
    			}
			inflate_done:
    			vec.resize(tmp_vec.size());
    			if (!tmp_vec.empty()) {
        			std::memcpy(vec.data(), tmp_vec.data(), tmp_vec.size());
    			}
    			std::vector<uint8_t>().swap(tmp_vec);
    			std::vector<uint8_t>().swap(buffer_vec);
		};
	
		constexpr uint32_t LARGE_FILE_SIZE = 300 * 1024 * 1024;
		const std::string LARGE_FILE_MSG = "\nPlease wait. Larger files will take longer to complete this process.\n";
		uint8_t byte_size = 4;
		
		if (args.mode == ArgMode::conceal) {  
			// --- Embed data file section code.		
			bool 
                       		hasMastodonOption = (args.platform == ArgOption::mastodon),
				hasRedditOption = (args.platform == ArgOption::reddit),
				hasNoneOption = (args.platform == ArgOption::none),
				hasTwitterBadDims = false;
                                               
        		validateDataFile(args.data_file, args.platform, cover_image_size, data_file_size, data_file_vec, isCompressedFile);
        				
			constexpr uint8_t 
				PNG_COLOR_TYPE_INDEX 	= 0x19,
				PNG_FIRST_BYTES 	= 33,
				PNG_IEND_BYTES 		= 12,
				PNG_TRUECOLOR	 	= 2,
				PNG_TRUECOLOR_ALPHA	= 6,
				PNG_INDEXED_COLOR	= 3;
		
			constexpr std::array<uint8_t, 4>
				PLTE_SIG 	{ 0x50, 0x4C, 0x54, 0x45 },
				TRNS_SIG 	{ 0x74, 0x52, 0x4E, 0x53 },
				IDAT_SIG 	{ 0x49, 0x44, 0x41, 0x54 };

			uint8_t image_color_type = cover_image_vec[PNG_COLOR_TYPE_INDEX];

       			if (image_color_type == PNG_TRUECOLOR || image_color_type == PNG_TRUECOLOR_ALPHA) {
       				std::vector<uint8_t> image; // Raw pixel data
    				lodepng::State state;
    		
    			unsigned 
    				width = 0,
    				height = 0,
    				error = lodepng::decode(image, width, height, state, cover_image_vec);
    			
    			if (error) {
        			throw std::runtime_error("Lodepng decoder error: " + std::to_string(error));
    			}
 
 	   		LodePNGColorStats stats;
    			lodepng_color_stats_init(&stats);
    			stats.allow_palette = 1; 
    			stats.allow_greyscale = 0;

    			error = lodepng_compute_color_stats(&stats, image.data(), width, height, &state.info_raw);
    			if (error) {
        			throw std::runtime_error("Lodepng stats error: " + std::to_string(error));
    			}
			constexpr uint16_t
				TWITTER_MAX_INDEX_DIMS = 4096,  // Indexed-color dims.
				TWITTER_MAX_TRUE_DIMS = 900,	// Truecolor dims.
				TWITTER_MIN_DIMS = 68;
			
			hasTwitterBadDims = (height > TWITTER_MAX_TRUE_DIMS || width > TWITTER_MAX_TRUE_DIMS || TWITTER_MIN_DIMS > height || TWITTER_MIN_DIMS > width);
			
    			if (stats.numcolors <= 256) {
    				std::cout << "\nWarning: Your cover image (PNG-32/24 Truecolor) has fewer than 257 unique colors.\n";
    				std::cout << "For compatibility reasons, the cover image will be converted to PNG-8 Indexed-Color.\n";
    			
        			std::vector<uint8_t> indexed_image(width * height);
        			std::vector<uint8_t> palette;
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
            				uint8_t r = image[i * channels];
            				uint8_t g = image[i * channels + 1];
            				uint8_t b = image[i * channels + 2];
            				uint8_t a = (channels == 4) ? image[i * channels + 3] : 255;

            				size_t index = 0;
            				for (size_t j = 0; j < palette_size; ++j) {
                				if (palette[j * 4] == r && palette[j * 4 + 1] == g &&
                    				palette[j * 4 + 2] == b && palette[j * 4 + 3] == a) {
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
            				lodepng_palette_add(&encodeState.info_png.color, palette[i * 4], 
                                		palette[i * 4 + 1], palette[i * 4 + 2], 
                                		palette[i * 4 + 3]);
            				lodepng_palette_add(&encodeState.info_raw, palette[i * 4], 
                                		palette[i * 4 + 1], palette[i * 4 + 2], 
                                		palette[i * 4 + 3]);
        			}

        			std::vector<uint8_t> output;
        			error = lodepng::encode(output, indexed_image.data(), width, height, encodeState);
        			if (error) {
            				throw std::runtime_error("Lodepng encode error: " + std::to_string(error));
        			}

        			cover_image_vec.swap(output);
        			std::vector<uint8_t>().swap(image);
        			std::vector<uint8_t>().swap(output);
        			std::vector<uint8_t>().swap(indexed_image);
        			std::vector<uint8_t>().swap(palette);
        		
        			std::cout << "\nCover image successfully converted to PNG-8 Indexed-Color (color type 3, 8-bit depth)." << std::endl;
        			image_color_type = cover_image_vec[PNG_COLOR_TYPE_INDEX];  
        			hasTwitterBadDims = (height > TWITTER_MAX_INDEX_DIMS || width > TWITTER_MAX_INDEX_DIMS || TWITTER_MIN_DIMS > height || TWITTER_MIN_DIMS > width);
    			}	 	
       		}	
    	
    		const uint32_t 
    			IMAGE_SIZE = static_cast<uint32_t>(cover_image_vec.size()),
    			FIRST_IDAT_INDEX = searchSig(cover_image_vec, 0, 0, IDAT_SIG);
    	
    		if (FIRST_IDAT_INDEX == IMAGE_SIZE) {
    			throw std::runtime_error("Image Error: Invalid or corrupt image file. Expected IDAT chunk not found!");
    		}
    	
    		std::vector<uint8_t> copied_image_vec;
    		copied_image_vec.reserve(IMAGE_SIZE);     
  
    		std::copy_n(cover_image_vec.begin(), PNG_FIRST_BYTES, std::back_inserter(copied_image_vec));	
	
    		auto copy_chunk_type = [&](const auto& chunk_signature) {
			constexpr uint8_t 
				PNG_CHUNK_FIELDS_COMBINED_LENGTH 	= 12, // Size_field + Name_field + CRC_field.
				PNG_CHUNK_LENGTH_FIELD_SIZE 		= 4,
				INCREMENT_NEXT_SEARCH_POS 		= 5;

			uint64_t chunk_length = 0;
        		uint32_t 
				chunk_search_pos 	= 0,
				chunk_length_pos 	= 0,
				chunk_count 		= 0;
		
        		while (true) {
            			chunk_search_pos = searchSig(cover_image_vec, chunk_search_pos, INCREMENT_NEXT_SEARCH_POS, chunk_signature);
            		
				if (chunk_signature != IDAT_SIG && chunk_search_pos > FIRST_IDAT_INDEX) {
					if (chunk_signature == PLTE_SIG && !chunk_count) {
						throw std::runtime_error("Image Error: Invalid or corrupt image file. Expected PLTE chunk not found!");
					} else {
						break;
					}
				} else if (chunk_search_pos == IMAGE_SIZE) {
					break;
				}
			
				++chunk_count;
			
				chunk_length_pos = chunk_search_pos - PNG_CHUNK_LENGTH_FIELD_SIZE;
				chunk_length = getValue(cover_image_vec, chunk_length_pos, byte_size) + PNG_CHUNK_FIELDS_COMBINED_LENGTH;
            		
	    			std::copy_n(cover_image_vec.begin() + chunk_length_pos, chunk_length, std::back_inserter(copied_image_vec));
        		}
    		};

		if (image_color_type == PNG_INDEXED_COLOR || image_color_type == PNG_TRUECOLOR) {
    			if (image_color_type == PNG_INDEXED_COLOR) {
    				copy_chunk_type(PLTE_SIG); 
    			}
    			copy_chunk_type(TRNS_SIG);
		}
	
    		copy_chunk_type(IDAT_SIG);

    		std::copy_n(cover_image_vec.end() - PNG_IEND_BYTES, PNG_IEND_BYTES, std::back_inserter(copied_image_vec));
    	
    		cover_image_vec.swap(copied_image_vec);
    		std::vector<uint8_t>().swap(copied_image_vec);
	
		std::string data_filename = args.data_file;

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

		if (data_file_size > LARGE_FILE_SIZE) {
			std::cout << LARGE_FILE_MSG;
		}

		// First, compress just the data file.
		zlibFunc(data_file_vec, args.mode);

		if (data_file_vec.empty()) {
			throw std::runtime_error("File Size Error: File is zero bytes. Probable compression failure.");
		}	
		// ------- Encrypt data file
		std::random_device rd;
 		std::mt19937 gen(rd());
		std::uniform_int_distribution<unsigned short> dis(1, 255); 
	
		constexpr uint8_t XOR_KEY_LENGTH = 24;
	
		uint16_t 
			data_filename_xor_key_index = hasMastodonOption ? 0x1A6 : 0x15, 
			data_filename_index = hasMastodonOption ? 0x192 : 0x01;  	
											
		uint8_t 
			data_filename_length = profile_vec[data_filename_index - 1],
			data_filename_char_pos = 0;

		std::generate_n(profile_vec.begin() + data_filename_xor_key_index, XOR_KEY_LENGTH, [&dis, &gen]() { return static_cast<uint8_t>(dis(gen)); });

		std::transform(
        		data_filename.begin() + data_filename_char_pos, data_filename.begin() + data_filename_char_pos + data_filename_length,
        		profile_vec.begin() + data_filename_xor_key_index, profile_vec.begin() + data_filename_index,
        		[](char a, uint8_t b) { return static_cast<uint8_t>(a) ^ b; }
    		);

		const uint32_t DATA_FILE_VEC_SIZE = static_cast<uint32_t>(data_file_vec.size());

		profile_vec.reserve(profile_vec.size() + DATA_FILE_VEC_SIZE);

		std::array<uint8_t, crypto_secretbox_KEYBYTES> key;	
    		crypto_secretbox_keygen(key.data());

		std::array<uint8_t, crypto_secretbox_NONCEBYTES> nonce; 
   		randombytes_buf(nonce.data(), nonce.size());

		const uint16_t
			SODIUM_KEY_INDEX = hasMastodonOption ? 0x1BE: 0x2D,     
			NONCE_KEY_INDEX  = hasMastodonOption ? 0x1DE: 0x4D;      
	
		std::copy_n(key.begin(), crypto_secretbox_KEYBYTES, profile_vec.begin() + SODIUM_KEY_INDEX); 	
		std::copy_n(nonce.begin(), crypto_secretbox_NONCEBYTES, profile_vec.begin() + NONCE_KEY_INDEX);

		std::vector<uint8_t> encrypted_vec(DATA_FILE_VEC_SIZE + crypto_secretbox_MACBYTES);

    		crypto_secretbox_easy(encrypted_vec.data(), data_file_vec.data(), DATA_FILE_VEC_SIZE, nonce.data(), key.data());
    		
    		std::vector<uint8_t>().swap(data_file_vec);

		std::copy_n(encrypted_vec.begin(), encrypted_vec.size(), std::back_inserter(profile_vec));

		std::vector<uint8_t>().swap(encrypted_vec);
		
		byte_size = 8;
		
		uint64_t pin = getValue(profile_vec, SODIUM_KEY_INDEX, byte_size);
	
		uint16_t 
			sodium_xor_key_pos = SODIUM_KEY_INDEX,
			sodium_key_pos = SODIUM_KEY_INDEX;

		uint8_t 
			sodium_keys_length = 48,
			value_bit_length = 64;
		
		sodium_key_pos += 8; 

		constexpr uint8_t SODIUM_XOR_KEY_LENGTH = 8;

		while (sodium_keys_length--) {   
    			profile_vec[sodium_key_pos] = profile_vec[sodium_key_pos] ^ profile_vec[sodium_xor_key_pos++];
			sodium_key_pos++;
    			sodium_xor_key_pos = (sodium_xor_key_pos >= SODIUM_XOR_KEY_LENGTH + SODIUM_KEY_INDEX) 
                        	 ? SODIUM_KEY_INDEX 
                        	 : sodium_xor_key_pos;
		}

		sodium_key_pos = SODIUM_KEY_INDEX;  

		std::mt19937_64 gen64(rd()); 
    		std::uniform_int_distribution<uint64_t> dis64; 

    		const uint64_t RANDOM_VAL = dis64(gen64); 

		updateValue(profile_vec, sodium_key_pos, RANDOM_VAL, value_bit_length);
	
		// -------- Encryption end.
	
		constexpr uint8_t PNG_END_BYTES_LENGTH = 12;

		if (hasRedditOption) {
			constexpr uint8_t CRC_LENGTH = 4;
			constexpr std::array<uint8_t, CRC_LENGTH> IDAT_REDDIT_CRC_BYTES { 0xA3, 0x1A, 0x50, 0xFA };

			std::vector<uint8_t>reddit_vec = { 0x00, 0x08, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54 };
			std::fill_n(std::back_inserter(reddit_vec), 0x80000, 0);

			std::copy_n(IDAT_REDDIT_CRC_BYTES.begin(), CRC_LENGTH, std::back_inserter(reddit_vec));
			
			cover_image_vec.insert(cover_image_vec.end() - PNG_END_BYTES_LENGTH, reddit_vec.begin(), reddit_vec.end());
			std::vector<uint8_t>().swap(reddit_vec);
			platforms_vec[0] = std::move(platforms_vec[4]);
			platforms_vec.resize(1);
		}

		uint32_t mastodon_deflate_size = 0;

		if (hasMastodonOption) {
			// Compresss the data chunk again, this time including the icc color profile. A requirement for iCCP chunk/Mastodon.
			zlibFunc(profile_vec, args.mode);
			mastodon_deflate_size = static_cast<uint32_t>(profile_vec.size());
		}

		constexpr uint8_t 
			CHUNK_START_INDEX = 4,
			MASTODON_SIZE_DIFF = 9,
			DEFAULT_SIZE_DIFF = 3;

		uint8_t chunk_size_index = 0;

		const uint32_t
			PROFILE_VEC_DEFLATE_SIZE = static_cast<uint32_t>(profile_vec.size()),
			IMAGE_VEC_SIZE = static_cast<uint32_t>(cover_image_vec.size()),
			CHUNK_SIZE = hasMastodonOption ? mastodon_deflate_size + MASTODON_SIZE_DIFF : PROFILE_VEC_DEFLATE_SIZE + DEFAULT_SIZE_DIFF,
			CHUNK_INDEX = hasMastodonOption ? 0x21 : IMAGE_VEC_SIZE - PNG_END_BYTES_LENGTH;

		const uint8_t PROFILE_VEC_INDEX = hasMastodonOption ? 0x0D : 0x0B;

		auto crcUpdate = [](uint8_t* buf, uint32_t buf_length) -> uint32_t {
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
		};	
	
		value_bit_length = 32;
	
		if (hasMastodonOption) {
			constexpr uint8_t MASTODON_ICCP_CHUNK_SIZE_DIFF = 5;
			constexpr uint16_t TWITTER_ICCP_MAX_CHUNK_SIZE = 10 * 1024;
			constexpr uint32_t TWITTER_IMAGE_MAX_SIZE = 5 * 1024 * 1024;
			
			const uint32_t MASTODON_CHUNK_SIZE = mastodon_deflate_size + MASTODON_ICCP_CHUNK_SIZE_DIFF;

			std::vector<uint8_t>iccp_vec = { 0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
			iccp_vec.reserve(iccp_vec.size() + PROFILE_VEC_DEFLATE_SIZE + CHUNK_SIZE);

			iccp_vec.insert((iccp_vec.begin() + PROFILE_VEC_INDEX), profile_vec.begin(), profile_vec.end());
			updateValue(iccp_vec, chunk_size_index, MASTODON_CHUNK_SIZE, value_bit_length);

			const uint32_t ICCP_CHUNK_CRC = crcUpdate(&iccp_vec[CHUNK_START_INDEX], CHUNK_SIZE);
			uint32_t iccp_crc_index = CHUNK_START_INDEX + CHUNK_SIZE;

			updateValue(iccp_vec, iccp_crc_index, ICCP_CHUNK_CRC, value_bit_length);
			cover_image_vec.insert((cover_image_vec.begin() + CHUNK_INDEX), iccp_vec.begin(), iccp_vec.end());
			
			std::vector<uint8_t>().swap(iccp_vec);
			
			if (!hasTwitterBadDims && TWITTER_IMAGE_MAX_SIZE >= cover_image_vec.size() && TWITTER_ICCP_MAX_CHUNK_SIZE >= MASTODON_CHUNK_SIZE) {
				platforms_vec[0] = std::move(platforms_vec[2]);
			} else {
				platforms_vec[0] = std::move(platforms_vec[3]);
			}
			platforms_vec.resize(1);
		} else {
			constexpr uint8_t 
				IDAT_CHUNK_SIZE_DIFF = 4,
				IDAT_CHUNK_CRC_INDEX_DIFF = 8;

	     		std::vector<uint8_t>idat_vec = { 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54, 0x78, 0x5E, 0x5C, 0x00, 0x00, 0x00, 0x00 };
			idat_vec.reserve(idat_vec.size() + profile_vec.size() + CHUNK_SIZE);
		
	     		idat_vec.insert(idat_vec.begin() + PROFILE_VEC_INDEX, profile_vec.begin(), profile_vec.end());		
			updateValue(idat_vec, chunk_size_index, CHUNK_SIZE, value_bit_length);

			const uint32_t IDAT_CHUNK_CRC = crcUpdate(&idat_vec[CHUNK_START_INDEX], CHUNK_SIZE + IDAT_CHUNK_SIZE_DIFF);
			uint32_t idat_crc_index = CHUNK_SIZE + IDAT_CHUNK_CRC_INDEX_DIFF;

			updateValue(idat_vec, idat_crc_index, IDAT_CHUNK_CRC, value_bit_length);
			cover_image_vec.insert((cover_image_vec.begin() + CHUNK_INDEX), idat_vec.begin(), idat_vec.end());

			std::vector<uint8_t>().swap(idat_vec);
		}
	
		std::vector<uint8_t>().swap(profile_vec);

    		std::uniform_int_distribution<> dist(10000, 99999);  

		const std::string OUTPUT_FILENAME = "prdt_" + std::to_string(dist(gen)) + ".png";

		std::ofstream file_ofs(OUTPUT_FILENAME, std::ios::binary);

		if (!file_ofs) {
			throw std::runtime_error("Write Error: Unable to write to file.");
		}
	
		const uint32_t OUTPUT_SIZE = static_cast<uint32_t>(cover_image_vec.size());

		file_ofs.write(reinterpret_cast<const char*>(cover_image_vec.data()), OUTPUT_SIZE);
		file_ofs.close();
		
		if (hasNoneOption) {
			platforms_vec.erase(platforms_vec.begin() + 2, platforms_vec.begin() + 5);
			
			constexpr uint32_t 
				FLICKR_MAX_IMAGE_SIZE = 200 * 1024 * 1024,
				IMGBB_POSTIMAGE_MAX_IMAGE_SIZE = 32 * 1024 * 1024,
				IMGPILE_MAX_IMAGE_SIZE = 8 * 1024 * 1024,
				TWITTER_MAX_IMAGE_SIZE = 5 * 1024 * 1024;
				
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
			std::vector<uint8_t>().swap(cover_image_vec);
		
		std::cout << "\nSaved \"file-embedded\" PNG image: " << OUTPUT_FILENAME << " (" << OUTPUT_SIZE << " bytes).\n";

		std::cout << "\nRecovery PIN: [***" << pin << "***]\n\nImportant: Keep your PIN safe, so that you can extract the hidden file.\n\nComplete!\n\n";
					 
		return 0;
	} else {
        	// ------- Recover data file section code.
        	constexpr uint8_t ICCP_CHUNK_INDEX = 0x25;

		constexpr std::array<uint8_t, 7> 
			ICCP_SIG 	{ 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63 },
			PDV_SIG 	{ 0xC6, 0x50, 0x3C, 0xEA, 0x5E, 0x9D, 0xF9 };
		
		const uint32_t
			ICCP_SIG_POS = searchSig(cover_image_vec, 0, 0, ICCP_SIG),
			PDV_SIG_POS  = searchSig(cover_image_vec, 0, 0, PDV_SIG);

		if (ICCP_SIG_POS != ICCP_CHUNK_INDEX && PDV_SIG_POS == cover_image_vec.size()) {
			throw std::runtime_error("Image File Error: This is not a pdvrdt image.");
		}

		bool isMastodonFile = (ICCP_SIG_POS == ICCP_CHUNK_INDEX && PDV_SIG_POS == cover_image_vec.size());
		
		constexpr uint8_t 
			DEFAULT_CHUNK_SIZE_INDEX_DIFF 	 = 112,
			DEFAULT_PROFILE_DATA_INDEX_DIFF  = 11,
			MASTODON_PROFILE_DATA_INDEX_DIFF = 9,
			MASTODON_CHUNK_SIZE_DIFF 	 = 9,
			MASTODON_CHUNK_SIZE_INDEX_DIFF 	 = 4,
			CHUNK_SIZE_DIFF 		 = 3;
	
		const uint32_t
			CHUNK_SIZE_INDEX = isMastodonFile ? ICCP_SIG_POS - MASTODON_CHUNK_SIZE_INDEX_DIFF : PDV_SIG_POS - DEFAULT_CHUNK_SIZE_INDEX_DIFF,				
			PROFILE_DATA_INDEX = isMastodonFile ? ICCP_SIG_POS + MASTODON_PROFILE_DATA_INDEX_DIFF : CHUNK_SIZE_INDEX + DEFAULT_PROFILE_DATA_INDEX_DIFF;
		
	const uint64_t CHUNK_SIZE = isMastodonFile ? getValue(cover_image_vec, CHUNK_SIZE_INDEX, byte_size) - MASTODON_CHUNK_SIZE_DIFF : getValue(cover_image_vec, CHUNK_SIZE_INDEX, byte_size);

	uint8_t byte = cover_image_vec.back();

	cover_image_vec.erase(cover_image_vec.begin(), cover_image_vec.begin() + PROFILE_DATA_INDEX);
	cover_image_vec.erase(cover_image_vec.begin() + (isMastodonFile ? CHUNK_SIZE + CHUNK_SIZE_DIFF : CHUNK_SIZE - CHUNK_SIZE_DIFF), cover_image_vec.end());
		
		if (isMastodonFile) {
			zlibFunc(cover_image_vec, args.mode);
			if (cover_image_vec.empty()) {
				// throw std::runtime_error("File Size Error: File is zero bytes. Probable failure inflating file.");
			}
			const uint32_t PDV_ICCP_SIG_POS = searchSig(cover_image_vec, 0, 0, PDV_SIG);
		
			if (PDV_ICCP_SIG_POS == cover_image_vec.size()) {
				throw std::runtime_error("Image File Error: This is not a pdvrdt image.");
			}
		}

		if (cover_image_size > LARGE_FILE_SIZE) {
			std::cout << LARGE_FILE_MSG;
		}

		const uint16_t 
			SODIUM_KEY_INDEX = isMastodonFile ? 0x1BE: 0x2D,
			NONCE_KEY_INDEX  = isMastodonFile ? 0x1DE: 0x4D;

		uint16_t 
			sodium_key_pos = SODIUM_KEY_INDEX,
			sodium_xor_key_pos = SODIUM_KEY_INDEX;
	
		uint8_t
			sodium_keys_length = 48,
			value_bit_length = 64;
		
		bool hasDecryptionFailed = false;
		
		std::cout << "\nPIN: ";
		
		const std::string MAX_UINT64_STR = "18446744073709551615";
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
	
    		uint64_t pin;
    		if (input.empty() || (input.length() == 20 && input > MAX_UINT64_STR)) {
        		pin = 0; 
    		} else {
        		pin = std::stoull(input); 
    		}
		
		updateValue(cover_image_vec, sodium_key_pos, pin, value_bit_length); 
	
		sodium_key_pos += 8;

		constexpr uint8_t SODIUM_XOR_KEY_LENGTH	= 8; 

		while(sodium_keys_length--) {
			cover_image_vec[sodium_key_pos] = cover_image_vec[sodium_key_pos] ^ cover_image_vec[sodium_xor_key_pos++];
			sodium_key_pos++;
			sodium_xor_key_pos = (sodium_xor_key_pos >= SODIUM_XOR_KEY_LENGTH + SODIUM_KEY_INDEX) 
				? SODIUM_KEY_INDEX 
				: sodium_xor_key_pos;
		}

		std::array<uint8_t, crypto_secretbox_KEYBYTES> key;
		std::array<uint8_t, crypto_secretbox_NONCEBYTES> nonce;

		std::copy(cover_image_vec.begin() + SODIUM_KEY_INDEX, cover_image_vec.begin() + SODIUM_KEY_INDEX + crypto_secretbox_KEYBYTES, key.data());
		std::copy(cover_image_vec.begin() + NONCE_KEY_INDEX, cover_image_vec.begin() + NONCE_KEY_INDEX + crypto_secretbox_NONCEBYTES, nonce.data());

		std::string decrypted_filename;

		const uint16_t ENCRYPTED_FILENAME_INDEX = isMastodonFile ? 0x192: 0x01;

		uint16_t filename_xor_key_pos = isMastodonFile ? 0x1A6 : 0x15;
	
		uint8_t
			encrypted_filename_length = cover_image_vec[ENCRYPTED_FILENAME_INDEX - 1],
			filename_char_pos = 0;

		const std::string ENCRYPTED_FILENAME { cover_image_vec.begin() + ENCRYPTED_FILENAME_INDEX, cover_image_vec.begin() + ENCRYPTED_FILENAME_INDEX + encrypted_filename_length };

		while (encrypted_filename_length--) {
			decrypted_filename += ENCRYPTED_FILENAME[filename_char_pos++] ^ cover_image_vec[filename_xor_key_pos++];
		}

		const uint16_t ENCRYPTED_FILE_START_INDEX = isMastodonFile ? 0x1FE: 0x6E;

		cover_image_vec.erase(cover_image_vec.begin(), cover_image_vec.begin() + ENCRYPTED_FILE_START_INDEX);

		std::vector<uint8_t>decrypted_vec(cover_image_vec.size() - crypto_secretbox_MACBYTES);
	
		if (crypto_secretbox_open_easy(decrypted_vec.data(), cover_image_vec.data(), cover_image_vec.size(), nonce.data(), key.data()) !=0 ) {
			std::cerr << "\nDecryption failed!" << std::endl;
			hasDecryptionFailed = true;
		}

		std::vector<uint8_t>().swap(cover_image_vec);
		
		if (hasDecryptionFailed) {	
			std::fstream file(args.cover_image, std::ios::in | std::ios::out | std::ios::binary); 
   		 
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
				std::ofstream file(args.cover_image, std::ios::out | std::ios::trunc | std::ios::binary);
			} else {
				file.seekp(byte_pos);
				file.write(reinterpret_cast<char*>(&byte), sizeof(byte));
			}

			file.close();

    	    		throw std::runtime_error("File Recovery Error: Invalid PIN or file is corrupt.");
		}
		
		zlibFunc(decrypted_vec, args.mode);

		const uint32_t INFLATED_FILE_SIZE = static_cast<uint32_t>(decrypted_vec.size());

		if (!INFLATED_FILE_SIZE) {
			throw std::runtime_error("Zlib Compression Error: Output file is empty. Inflating file failed.");
		}

		if (byte != 0x82) {	
			std::fstream file(args.cover_image, std::ios::in | std::ios::out | std::ios::binary); 
   		 
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

		file_ofs.write(reinterpret_cast<const char*>(decrypted_vec.data()), INFLATED_FILE_SIZE);
		file_ofs.close();

		std::vector<uint8_t>().swap(decrypted_vec);

		std::cout << "\nExtracted hidden file: " << decrypted_filename << " (" << INFLATED_FILE_SIZE << " bytes).\n\nComplete! Please check your file.\n\n";

		return 0;
	    	}   
	}
	catch (const std::runtime_error& e) {
        	std::cerr << "\n" << e.what() << "\n\n";
        	return 1;
    	}
}
