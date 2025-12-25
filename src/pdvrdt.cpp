// PNG Data Vehicle (pdvrdt v4.3). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023

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
#include <bit>
#include <cstdint>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <initializer_list>
#include <limits>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {
	using Byte    = std::uint8_t;
	using vBytes  = std::vector<Byte>;
	using vString = std::vector<std::string>;

	using Key   = std::array<Byte, crypto_secretbox_KEYBYTES>;
	using Nonce = std::array<Byte, crypto_secretbox_NONCEBYTES>;
	using Tag   = std::array<Byte, crypto_secretbox_MACBYTES>;

	constexpr std::size_t TAG_BYTES = Tag{}.size();

	enum class Mode 	: Byte { conceal, recover };
	enum class Option 	: Byte { None, Mastodon, Reddit };

	enum class FileTypeCheck : Byte {
		cover_image    = 1, // Conceal mode...
		embedded_image = 2, // Recover mode...
		data_file 	   = 3  // Conceal mode...
	};

	void displayInfo() {
		std::print(R"(

PNG Data Vehicle (pdvrdt v4.3)
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

)");
	}

	struct ProgramArgs {
		Mode mode{Mode::conceal};
		Option option{Option::None};
		fs::path image_file_path;
		fs::path data_file_path;

	private:
		[[noreturn]] static void die(std::string_view usage) {
			throw std::runtime_error(std::string(usage));
		}

	public:
		static std::optional<ProgramArgs> parse(int argc, char** argv) {
			auto arg = [&](int i) -> std::string_view {
				return (i >= 0 && i < argc) ? std::string_view(argv[i]) : std::string_view{};
			};

			constexpr std::string_view PREFIX = "Usage: ";
			const std::string
            PROG = fs::path(argv[0]).filename().string(),
            INDENT(PREFIX.size(), ' '),
            USAGE = std::format("{}{} conceal [-m|-r] <secret_file>\n""{}{} recover <cover_image>\n""{}{} --info", PREFIX, PROG, INDENT, PROG, INDENT, PROG);

			if (argc < 2) die(USAGE);

			if (argc == 2 && arg(1) == "--info") {
				displayInfo();
				return std::nullopt;
			}

			ProgramArgs out{};
			const std::string_view MODE = arg(1);

			if (MODE == "conceal") {
				int i = 2;
				if (arg(i) == "-m" || arg(i) == "-r") {
					out.option = (arg(i) == "-m") ? Option::Mastodon : Option::Reddit;
					++i;
				}

				if (argc != i + 2) die(USAGE);
				if (arg(i).empty() || arg(i + 1).empty()) die(USAGE);
				out.image_file_path = fs::path(arg(i));
				out.data_file_path  = fs::path(arg(i + 1));
				out.mode = Mode::conceal;
				return out;
			}

			if (MODE == "recover") {
				if (argc != 3) die(USAGE);
				if (arg(2).empty()) die(USAGE);
				out.image_file_path = fs::path(arg(2));
				out.mode = Mode::recover;
				return out;
			}
        die(USAGE);
		}
	};

	[[nodiscard]] std::optional<std::size_t> searchSig(std::span<const Byte> data, std::span<const Byte> sig, std::size_t start_index = 0, std::size_t increment = 0) {
		if (start_index >= data.size()) {
			return std::nullopt;
		}
		auto search_range = std::views::drop(data, start_index + increment);
		auto result = std::ranges::search(search_range, sig);
		if (result.empty()) {
			return std::nullopt;
		}
		return static_cast<std::size_t>(result.begin() - data.begin());
	}

	void updateValue(std::span<Byte> data, std::size_t index, std::size_t value, std::size_t length = 4, bool isBigEndian = true) {
		std::size_t write_index = isBigEndian ? index : index - (length - 1);

		if (write_index + length > data.size()) {  // ← Changed from vec.size()
			throw std::out_of_range("updateValue: Index out of bounds.");
		}

		switch (length) {
			case 4: {
				uint32_t val = static_cast<uint32_t>(value);
				if (isBigEndian) {
					val = std::byteswap(val);
				}
				std::memcpy(data.data() + write_index, &val, length);
				break;
			}
			case 8: {
				uint64_t val = static_cast<uint64_t>(value);
				if (isBigEndian) {
					val = std::byteswap(val);
				}
				std::memcpy(data.data() + write_index, &val, length);
				break;
			}
			default:
				std::unreachable();
		}
	}

	[[nodiscard]] std::size_t getValue(std::span<const Byte> data, std::size_t index, std::size_t length = 4) {
		if (index + length > data.size()) {
			throw std::out_of_range("getValue: Index out of bounds");
		}

		switch (length) {
			case 4: {
				uint32_t value;
				std::memcpy(&value, data.data() + index, 4);
				return std::byteswap(value);
			}
			case 8: {
				uint64_t value;
				std::memcpy(&value, data.data() + index, 8);
				return std::byteswap(value);
			}
			default:
				std::unreachable();
		}
	}

	void imageColorCheck(vBytes& image_file_vec, bool& hasTwitterBadDims) {
		constexpr Byte
			TRUECOLOR_RGB	  = 2,
			TRUECOLOR_RGBA	  = 6,
			INDEXED_PLTE 	  = 3,
			PALETTE_BIT_DEPTH = 8,
			TWITTER_MIN_DIMS  = 68,
			ALPHA_OPAQUE 	  = 255;

		constexpr std::size_t
			RGBA_COMPONENTS = 4,
			RGB_COMPONENTS  = 3;
	
		constexpr uint16_t
			TWITTER_MAX_PLTE_DIMS = 4096,
			TWITTER_MAX_RGB_DIMS  = 900,
			MIN_RGB_COLORS	      = 257;

		vBytes image;

		lodepng::State state;
		state.decoder.zlibsettings.ignore_adler32 = 1;

		unsigned
			width  = 0,
			height = 0,
			error  = lodepng::decode(image, width, height, state, image_file_vec);

		if (error) {
			throw std::runtime_error(std::format("LodePNG Error: {}: {}", error, lodepng_error_text(error)));
		}

		LodePNGColorStats stats;
		lodepng_color_stats_init(&stats);
		stats.allow_palette   = 1;
		stats.allow_greyscale = 0;
		error = lodepng_compute_color_stats(&stats, image.data(), width, height, &state.info_raw);

		if (error) {
			throw std::runtime_error("LodePNG stats error: " + std::to_string(error));
		}

		hasTwitterBadDims = (height > TWITTER_MAX_RGB_DIMS || width > TWITTER_MAX_RGB_DIMS || TWITTER_MIN_DIMS > height || TWITTER_MIN_DIMS > width);
		Byte color_type = static_cast<Byte>(state.info_png.color.colortype);

		if ((color_type == TRUECOLOR_RGB || color_type == TRUECOLOR_RGBA) && (MIN_RGB_COLORS > stats.numcolors)) {

			const std::size_t
				PALETTE_SIZE = stats.numcolors,
				CHANNELS 	 = (state.info_raw.colortype == LCT_RGBA) ? RGBA_COMPONENTS : RGB_COMPONENTS;

			vBytes palette(PALETTE_SIZE * RGBA_COMPONENTS);
			std::unordered_map<uint32_t, Byte> color_to_index;

			for (std::size_t i = 0; i < PALETTE_SIZE; ++i) {
				const Byte* P = &stats.palette[i * RGBA_COMPONENTS];
				palette[i * RGBA_COMPONENTS]     = P[0];
				palette[i * RGBA_COMPONENTS + 1] = P[1];
				palette[i * RGBA_COMPONENTS + 2] = P[2];
				palette[i * RGBA_COMPONENTS + 3] = P[3];
				const uint32_t KEY = (P[0] << 24) | (P[1] << 16) | (P[2] << 8) | P[3];
				color_to_index[KEY] = static_cast<Byte>(i);
			}

			vBytes indexed_image(width * height);
			for (std::size_t i = 0; i < width * height; ++i) {
				const Byte
					R = image[i * CHANNELS],
					G = image[i * CHANNELS + 1],
					B = image[i * CHANNELS + 2],
					A = (CHANNELS == RGBA_COMPONENTS) ? image[i * CHANNELS + 3] : ALPHA_OPAQUE;

				const uint32_t KEY = (R << 24) | (G << 16) | (B << 8) | A;
				indexed_image[i] = color_to_index[KEY];
			}

			lodepng::State encodeState;
			encodeState.info_raw.colortype 		 = LCT_PALETTE;
			encodeState.info_raw.bitdepth 		 = PALETTE_BIT_DEPTH;
			encodeState.info_png.color.colortype = LCT_PALETTE;
			encodeState.info_png.color.bitdepth  = PALETTE_BIT_DEPTH;
			encodeState.encoder.auto_convert 	 = 0;

			for (std::size_t i = 0; i < PALETTE_SIZE; ++i) {
				const Byte* P = &palette[i * RGBA_COMPONENTS];
				lodepng_palette_add(&encodeState.info_png.color, P[0], P[1], P[2], P[3]);
				lodepng_palette_add(&encodeState.info_raw, P[0], P[1], P[2], P[3]);
			}

			vBytes output;
			error = lodepng::encode(output, indexed_image.data(), width, height, encodeState);
			if (error) {
				throw std::runtime_error("LodePNG encode error: " + std::to_string(error));
			}
			image_file_vec = std::move(output);
			hasTwitterBadDims = (height > TWITTER_MAX_PLTE_DIMS || width > TWITTER_MAX_PLTE_DIMS || TWITTER_MIN_DIMS > height || TWITTER_MIN_DIMS > width);
		} else {
			// Because image was NOT re-encoded (it had more than 256 colors), we need to manually
			// check for and remove trailing data and also remove superfluous chunks.
			constexpr auto
				PLTE_SIG = std::to_array<Byte>({ 0x50, 0x4C, 0x54, 0x45 }),
				TRNS_SIG = std::to_array<Byte>({ 0x74, 0x52, 0x4E, 0x53 }),
				IDAT_SIG = std::to_array<Byte>({ 0x49, 0x44, 0x41, 0x54 });

			constexpr auto IEND_SIG = std::to_array<Byte>({ 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 });

			// Check for and remove any trailing data after IEND.
			if (auto pos_opt = searchSig(image_file_vec, IEND_SIG)) {
				constexpr std::size_t NAME_FIELD_CRC_FIELD_COMBINED_LENGTH = 8;
				const std::size_t PNG_STANDARD_END_INDEX = *pos_opt + NAME_FIELD_CRC_FIELD_COMBINED_LENGTH;

				if (PNG_STANDARD_END_INDEX <= image_file_vec.size()) {
					image_file_vec.resize(PNG_STANDARD_END_INDEX);
				}
			}

			// Make a copy of the cover image and only keep required chunks.
			constexpr std::size_t
				PNG_FIRST_BYTES = 33,
				PNG_IEND_BYTES  = 12;

			vBytes copied_png;
			copied_png.reserve(image_file_vec.size());

			// Copy PNG header + IHDR chunk
			copied_png.insert(copied_png.end(), image_file_vec.begin(), image_file_vec.begin() + PNG_FIRST_BYTES);

			auto copy_chunk_type = [&](const auto& chunk_signature) {
				constexpr std::size_t
					CHUNK_FIELDS_COMBINED_LENGTH = 12,
					CHUNK_LENGTH_FIELD_SIZE      = 4,
					VALUE_BYTE_LENGTH			 = 4;

				std::size_t
					search_start 	 = 0,
					increment    	 = 0,
					chunk_name_index = 0,
					chunk_start 	 = 0,
					chunk_length 	 = 0;

				while (auto chunk_opt = searchSig(image_file_vec, chunk_signature, search_start, increment)) {
					chunk_name_index = *chunk_opt;

					// Check for and skip pdvrdt IDAT chunk, just in case someone uses a pdvrdt png image as their cover image.
					if (image_file_vec[chunk_name_index + 4] == 0x78 &&
						image_file_vec[chunk_name_index + 5] == 0x5E &&
						image_file_vec[chunk_name_index + 6] == 0x5C) {
						break;
					}

					chunk_start = chunk_name_index - CHUNK_LENGTH_FIELD_SIZE;
					chunk_length = getValue(image_file_vec, chunk_start, VALUE_BYTE_LENGTH) + CHUNK_FIELDS_COMBINED_LENGTH;

					copied_png.insert(copied_png.end(), image_file_vec.begin() + chunk_start, image_file_vec.begin() + chunk_start + chunk_length);

					search_start = chunk_name_index;
					increment = 5;
				}
			};

			if (color_type == INDEXED_PLTE || color_type == TRUECOLOR_RGB) {
				if (color_type == INDEXED_PLTE) {
					copy_chunk_type(PLTE_SIG);
				}
				copy_chunk_type(TRNS_SIG);
			}
			copy_chunk_type(IDAT_SIG);

			// Copy IEND chunk
			copied_png.insert(copied_png.end(), image_file_vec.end() - PNG_IEND_BYTES, image_file_vec.end());

			image_file_vec = std::move(copied_png);
		}
	}

	[[nodiscard]] std::size_t encryptDataFile(vBytes& profile_vec, vBytes& data_vec, std::string& data_filename, bool hasMastodonOption) {
		const std::size_t
			DATA_FILENAME_XOR_KEY_INDEX = hasMastodonOption ? 0x1A6 : 0x15,
			DATA_FILENAME_INDEX         = hasMastodonOption ? 0x192 : 0x01,
			SODIUM_KEY_INDEX 	    	= hasMastodonOption ? 0x1BE : 0x2D, // 32 bytes
			NONCE_KEY_INDEX  	    	= hasMastodonOption ? 0x1DE : 0x4D; // 24 bytes

		const Byte DATA_FILENAME_LENGTH = profile_vec[DATA_FILENAME_INDEX - 1];

		randombytes_buf(profile_vec.data() + DATA_FILENAME_XOR_KEY_INDEX, DATA_FILENAME_LENGTH);

		std::ranges::transform(
			data_filename | std::views::take(DATA_FILENAME_LENGTH),
			profile_vec | std::views::drop(DATA_FILENAME_XOR_KEY_INDEX) | std::views::take(DATA_FILENAME_LENGTH),
			profile_vec.begin() + DATA_FILENAME_INDEX,
			[](char a, Byte b) { return static_cast<Byte>(a) ^ b; }
		);

		Key   key{};
		Nonce nonce{};

		crypto_secretbox_keygen(key.data());
		randombytes_buf(nonce.data(), nonce.size());

		std::ranges::copy(key, profile_vec.begin() + SODIUM_KEY_INDEX);
		std::ranges::copy(nonce, profile_vec.begin() + NONCE_KEY_INDEX);

		const std::size_t DATA_LENGTH = data_vec.size();
		data_vec.resize(DATA_LENGTH + TAG_BYTES);

		if (crypto_secretbox_easy(data_vec.data(), data_vec.data(), DATA_LENGTH, nonce.data(), key.data()) != 0) {
			sodium_memzero(key.data(),   key.size());
			sodium_memzero(nonce.data(), nonce.size());
			throw std::runtime_error("crypto_secretbox_easy failed");
		}

		profile_vec.reserve(profile_vec.size() + data_vec.size());

		profile_vec.insert(profile_vec.end(), data_vec.begin(), data_vec.end());

		vBytes().swap(data_vec);

		constexpr std::size_t
			SODIUM_XOR_KEY_LENGTH = 8,
			VALUE_BYTE_LENGTH 	  = 8;

		std::size_t
			pin                = getValue(profile_vec, SODIUM_KEY_INDEX, VALUE_BYTE_LENGTH),
			sodium_keys_length = 48,
			sodium_xor_key_pos = SODIUM_KEY_INDEX,
			sodium_key_pos     = SODIUM_KEY_INDEX + SODIUM_XOR_KEY_LENGTH;

		while (sodium_keys_length--) {
			profile_vec[sodium_key_pos++] ^= profile_vec[sodium_xor_key_pos++];
			if (sodium_xor_key_pos >= SODIUM_KEY_INDEX + SODIUM_XOR_KEY_LENGTH) {
				sodium_xor_key_pos = SODIUM_KEY_INDEX;
			}
		}

		std::size_t random_val;
		randombytes_buf(&random_val, sizeof random_val);

		updateValue(profile_vec, SODIUM_KEY_INDEX, random_val, VALUE_BYTE_LENGTH);

		sodium_memzero(key.data(),   key.size());
		sodium_memzero(nonce.data(), nonce.size());

		return pin;
	}

	struct SyncGuard {
		bool old_status;
		SyncGuard() : old_status(std::cout.sync_with_stdio(false)) {}
        ~SyncGuard() { std::cout.sync_with_stdio(old_status); }
    };

    #ifndef _WIN32
		struct TermiosGuard {
			termios old;
			TermiosGuard() {
				tcgetattr(STDIN_FILENO, &old);
				termios newt = old;
				newt.c_lflag &= ~(ICANON | ECHO);
				tcsetattr(STDIN_FILENO, TCSANOW, &newt);
            }
            ~TermiosGuard() { tcsetattr(STDIN_FILENO, TCSANOW, &old); }
        };
    #endif

	[[nodiscard]] std::size_t getPin() {
		constexpr auto MAX_UINT64_STR = std::string_view{"18446744073709551615"};
		constexpr std::size_t MAX_PIN_LENGTH = 20;

		std::print("\nPIN: ");
		std::fflush(stdout);
		std::string input;
		char ch;
		bool sync_status = std::cout.sync_with_stdio(false);

		#ifdef _WIN32
			while (input.length() < MAX_PIN_LENGTH) {
				ch = _getch();
				if (ch >= '0' && ch <= '9') {
					input.push_back(ch);
					std::print("*");
					std::fflush(stdout);
				} else if (ch == '\b' && !input.empty()) {
					std::print("\b \b");
					std::fflush(stdout);
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

			while (input.length() < MAX_PIN_LENGTH) {
				ssize_t bytes_read = read(STDIN_FILENO, &ch, 1);
				if (bytes_read <= 0) continue;

				if (ch >= '0' && ch <= '9') {
					input.push_back(ch);
					std::print("*");
					std::fflush(stdout);
				} else if ((ch == '\b' || ch == 127) && !input.empty()) {
					std::print("\b \b");
					std::fflush(stdout);
					input.pop_back();
				} else if (ch == '\n') {
					break;
				}
			}
			tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
		#endif

		std::println("");
		std::fflush(stdout);
		std::cout.sync_with_stdio(sync_status);

		if (input.empty() || (input.length() == MAX_PIN_LENGTH && input > MAX_UINT64_STR)) {
			return 0;
		}
		return std::stoull(input);
	}

	[[nodiscard]] bool hasValidFilename(const fs::path& p) {
		if (p.empty()) {
			return false;
		}
		const auto FILENAME = p.filename().string();
		if (FILENAME.empty()) {
			return false;
		}

		return std::ranges::all_of(FILENAME, [](unsigned char c) {
			return std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == '@' || c == '%';
		});
	}

	[[nodiscard]] bool hasFileExtension(const fs::path& p, std::initializer_list<std::string_view> exts) {
		auto e = p.extension().string();
		std::ranges::transform(e, e.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});

		return std::ranges::any_of(exts, [&e](std::string_view ext) {
			std::string c{ext};
			std::ranges::transform(c, c.begin(), [](unsigned char x) {
				return static_cast<char>(std::tolower(x));
			});
			return e == c;
		});
	}

	[[nodiscard]] std::string decryptDataFile(vBytes& png_vec, bool isMastodonFile, bool& hasDecryptionFailed) {
		constexpr std::size_t
			SODIUM_XOR_KEY_LENGTH = 8,
			VALUE_BYTE_LENGTH     = 8;

		const std::size_t
			SODIUM_KEY_INDEX           = isMastodonFile ? 0x1BE : 0x2D,
			NONCE_KEY_INDEX            = isMastodonFile ? 0x1DE : 0x4D,
			ENCRYPTED_FILENAME_INDEX   = isMastodonFile ? 0x192 : 0x01,
			FILENAME_XOR_KEY_INDEX     = isMastodonFile ? 0x1A6 : 0x15,
			ENCRYPTED_FILE_START_INDEX = isMastodonFile ? 0x1FE : 0x6E,
			CIPH_LENGTH                = png_vec.size() - ENCRYPTED_FILE_START_INDEX,
			FILENAME_LENGTH_INDEX      = ENCRYPTED_FILENAME_INDEX - 1;

		std::size_t
			recovery_pin       = getPin(),
			sodium_keys_length = 48,
			sodium_xor_key_pos = SODIUM_KEY_INDEX,
			sodium_key_pos     = SODIUM_KEY_INDEX + SODIUM_XOR_KEY_LENGTH;

		updateValue(png_vec, SODIUM_KEY_INDEX, recovery_pin, VALUE_BYTE_LENGTH);

		while (sodium_keys_length--) {
			png_vec[sodium_key_pos] ^= png_vec[sodium_xor_key_pos++];
			sodium_key_pos++;
			if (sodium_xor_key_pos >= SODIUM_KEY_INDEX + SODIUM_XOR_KEY_LENGTH) {
				sodium_xor_key_pos = SODIUM_KEY_INDEX;
			}
		}

		Key   key{};
		Nonce nonce{};

		std::ranges::copy_n(png_vec.begin() + SODIUM_KEY_INDEX, key.size(), key.data());
		std::ranges::copy_n(png_vec.begin() + NONCE_KEY_INDEX, nonce.size(), nonce.data());

		const Byte FILENAME_LENGTH = png_vec[FILENAME_LENGTH_INDEX];

		std::string decrypted_filename(FILENAME_LENGTH, '\0');

		std::ranges::transform(
			png_vec | std::views::drop(ENCRYPTED_FILENAME_INDEX) | std::views::take(FILENAME_LENGTH),
			png_vec | std::views::drop(FILENAME_XOR_KEY_INDEX) | std::views::take(FILENAME_LENGTH),
			decrypted_filename.begin(),
			[](Byte a, Byte b) { return static_cast<char>(a ^ b); }
		);

		Byte* dest = png_vec.data();
		const Byte* CIPHER_SRC = png_vec.data() + ENCRYPTED_FILE_START_INDEX;

		if (crypto_secretbox_open_easy(dest, CIPHER_SRC, CIPH_LENGTH, nonce.data(), key.data()) != 0) {
			std::println(std::cerr, "\nDecryption failed!");
			hasDecryptionFailed = true;
			sodium_memzero(key.data(),   key.size());
			sodium_memzero(nonce.data(), nonce.size());
			return {};
		}

		png_vec.resize(CIPH_LENGTH - TAG_BYTES);

		sodium_memzero(key.data(),   key.size());
		sodium_memzero(nonce.data(), nonce.size());

		return decrypted_filename;
	}

	// Zlib function, deflate or inflate data file within vector.
	void zlibFunc(vBytes& data_vec, Mode mode, Option option, bool isCompressedFile = false) {
		constexpr std::size_t BUFSIZE = 2 * 1024 * 1024;

		vBytes buffer_vec(BUFSIZE);
		vBytes tmp_vec;
		tmp_vec.reserve(data_vec.size() + BUFSIZE);

		z_stream strm{};
		strm.next_in  = data_vec.data();
		strm.avail_in = static_cast<uInt>(std::min(data_vec.size(), static_cast<size_t>(std::numeric_limits<uInt>::max())));

		strm.next_out  = buffer_vec.data();
		strm.avail_out = static_cast<uInt>(BUFSIZE);

		auto append_written_and_reset_out = [&](z_stream& s) {
			const std::size_t WRITTEN = BUFSIZE - s.avail_out;
			if (WRITTEN > 0) {
				tmp_vec.insert(tmp_vec.end(), buffer_vec.begin(), buffer_vec.begin() + WRITTEN);
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
				const std::size_t WRITTEN = BUFSIZE - s.avail_out;
				if (WRITTEN > 0) {
					tmp_vec.insert(tmp_vec.end(), buffer_vec.begin(), buffer_vec.begin() + WRITTEN);
				}
				s.next_out  = buffer_vec.data();
				s.avail_out = static_cast<uInt>(BUFSIZE);
			};

			while (strm.avail_in > 0) {
				int ret = inflate(&strm, Z_NO_FLUSH);

				if (ret == Z_STREAM_END) {
					spill(strm);
					inflateEnd(&strm);
					data_vec = std::move(tmp_vec);
					return;
				}

				if (ret != Z_OK) {
					inflateEnd(&strm);
					throw std::runtime_error(std::format("zlib inflate error: {}", strm.msg ? strm.msg : std::to_string(ret)));
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
					throw std::runtime_error(std::format("Zlib Inflate Error during finish: {}", strm.msg ? strm.msg : std::to_string(ret)));
				}

				inflateEnd(&strm);
				throw std::runtime_error("Zlib Inflate: unexpected return code");
			}
			inflateEnd(&strm);
		}
		data_vec = std::move(tmp_vec);
	}

	[[nodiscard]] vBytes readFile(const fs::path& path, FileTypeCheck FileType = FileTypeCheck::data_file) {
		if (!fs::exists(path) || !fs::is_regular_file(path)) {
			throw std::runtime_error(std::format("Error: File \"{}\" not found or not a regular file.", path.string()));
		}

		std::size_t file_size = fs::file_size(path);

		if (!file_size) {
			throw std::runtime_error("Error: File is empty.");
		}

		if (FileType == FileTypeCheck::cover_image || FileType == FileTypeCheck::embedded_image) {
			if (!hasFileExtension(path, {".png"})) {
				throw std::runtime_error("File Type Error: Invalid image extension. Only expecting \".png\".");
			}

			if (FileType == FileTypeCheck::cover_image) {
				constexpr std::size_t MINIMUM_IMAGE_SIZE = 87;

				if (MINIMUM_IMAGE_SIZE > file_size) {
					throw std::runtime_error("File Error: Invalid image file size.");
				}

				constexpr std::size_t MAX_IMAGE_SIZE = 8 * 1024 * 1024;

				if (file_size > MAX_IMAGE_SIZE) {
					throw std::runtime_error("Image File Error: Cover image file exceeds maximum size limit.");
				}
			}
		}

		constexpr std::size_t MAX_FILE_SIZE = 3ULL * 1024 * 1024 * 1024;

		if (file_size > MAX_FILE_SIZE) {
			throw std::runtime_error("Error: File exceeds program size limit.");
		}

		if (!hasValidFilename(path)) {
			throw std::runtime_error("Invalid Input Error: Unsupported characters in filename arguments.");
		}

		std::ifstream file(path, std::ios::binary);

		if (!file) {
			throw std::runtime_error(std::format("Failed to open file: {}", path.string()));
		}

		vBytes vec(file_size);
		file.read(reinterpret_cast<char*>(vec.data()), static_cast<std::streamsize>(file_size));

		if (file.gcount() != static_cast<std::streamsize>(file_size)) {
			throw std::runtime_error("Failed to read full file: partial read");
		}
		return vec;
	}

	int concealData(vBytes& png_vec, Mode mode, Option option, fs::path& data_file_path) {
		vString platforms_vec {
					"X-Twitter", "ImgPile", "Mastodon and X-Twitter.", "Mastodon. (Only share this \"file-embedded\" PNG image on Mastodon).",
					"Reddit. (Only share this \"file-embedded\" PNG image on Reddit).", "PostImage", "ImgBB", "Flickr"
				};
		bool
			isCompressedFile  = false,
			hasTwitterBadDims = false,
			hasMastodonOption = (option == Option::Mastodon),
			hasRedditOption   = (option == Option::Reddit),
			hasNoOption		  = (option == Option::None);

		vBytes data_vec = readFile(data_file_path);
		std::size_t data_vec_size = data_vec.size();

		imageColorCheck(png_vec, hasTwitterBadDims);

		std::size_t png_vec_size = png_vec.size();

		constexpr std::size_t
			MAX_SIZE_CONCEAL   		 = 2ULL * 1024 * 1024 * 1024,
			LARGE_FILE_SIZE 		 = 300  * 1024 * 1024,
			MAX_SIZE_REDDIT    		 = 20 	* 1024 * 1024,
			MAX_SIZE_MASTODON  		 = 16 	* 1024 * 1024,
			DATA_FILENAME_MAX_LENGTH = 20;

		const std::size_t COMBINED_FILE_SIZE = data_vec_size + png_vec_size;

		if (data_vec_size > LARGE_FILE_SIZE) {
			std::println("\nPlease wait. Larger files will take longer to complete this process.");
		}

		if (hasMastodonOption && COMBINED_FILE_SIZE > MAX_SIZE_MASTODON) {
			throw std::runtime_error("Data File Size Error: Combined size of image and data file exceeds maximum size limit for the Mastodon platform.");
		}

		if (hasRedditOption && COMBINED_FILE_SIZE > MAX_SIZE_REDDIT) {
			throw std::runtime_error("File Size Error: Combined size of image and data file exceeds maximum size limit for the Reddit platform.");
		}

		if (hasNoOption && COMBINED_FILE_SIZE > MAX_SIZE_CONCEAL) {
			throw std::runtime_error("File Size Error: Combined size of image and data file exceeds maximum default size limit for pdvrdt.");
		}

		std::string data_filename = data_file_path.filename().string();

		if (data_filename.size() > DATA_FILENAME_MAX_LENGTH) {
			throw std::runtime_error("Data File Error: For compatibility requirements, length of data filename must not exceed 20 characters.");
		}

		isCompressedFile = hasFileExtension(data_file_path, {
			".zip",".jar",".rar",".7z",".bz2",".gz",".xz",".tar",".lz",".lz4",".cab",".rpm",".deb",
			".mp4",".mp3",".exe",".jpg",".jpeg",".jfif",".png",".webp",".bmp",".gif",".ogg",".flac"
		});

		// Default vector, where data is stored in the last IDAT chunk of the PNG cover image.
		vBytes default_vec {
			0x75, 0x5D, 0x19, 0x3D, 0x72, 0xCE, 0x28, 0xA5, 0x60, 0x59, 0x17, 0x98, 0x13, 0x40, 0xB4, 0xDB, 0x3D, 0x18, 0xEC, 0x10, 0xFA, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0x50, 0x3C, 0xEA, 0x5E, 0x9D, 0xF9, 0x90, 0x82
		};

		// COLOR PROFILE FOR PNG / Mastodon, where data need to be stored in the iCCP chunk. 0x1FE
		vBytes mastodon_vec {
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

		vBytes profile_vec;

		if (hasMastodonOption) {
			profile_vec = std::move(mastodon_vec);
			vBytes().swap(default_vec);
		} else {
			profile_vec = std::move(default_vec);
			vBytes().swap(mastodon_vec);
		}

		const std::size_t PROFILE_NAME_LENGTH_INDEX = hasMastodonOption ? 0x191 : 0x00;

		profile_vec[PROFILE_NAME_LENGTH_INDEX] = static_cast<Byte>(data_filename.size());

		// First, compress just the data file.
		zlibFunc(data_vec, mode, option, isCompressedFile);

		if (data_vec.empty()) {
			throw std::runtime_error("File Size Error: File is zero bytes. Probable compression failure.");
		}

		const std::size_t PIN = encryptDataFile(profile_vec, data_vec, data_filename, hasMastodonOption);

		if (hasRedditOption) {
			constexpr std::size_t INSERT_INDEX_DIFF = 12;

			constexpr auto IDAT_REDDIT_CRC_BYTES = std::to_array<Byte>({ 0xA3, 0x1A, 0x50, 0xFA });

			vBytes reddit_vec = { 0x00, 0x08, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54 };
			std::fill_n(std::back_inserter(reddit_vec), 0x80000, 0);

			reddit_vec.insert(reddit_vec.end(), IDAT_REDDIT_CRC_BYTES.begin(), IDAT_REDDIT_CRC_BYTES.end());

			png_vec.reserve(png_vec.size() + reddit_vec.size());

			png_vec.insert(png_vec.end() - INSERT_INDEX_DIFF, reddit_vec.begin(), reddit_vec.end());
			vBytes().swap(reddit_vec);

			platforms_vec[0] = std::move(platforms_vec[4]);
			platforms_vec.resize(1);
		}

		std::size_t mastodon_deflate_size = 0;

		if (hasMastodonOption) {
			// Compresss the data chunk again, this time including the icc color profile. A requirement for iCCP chunk/Mastodon.
			zlibFunc(profile_vec, mode, option, isCompressedFile);
			mastodon_deflate_size = profile_vec.size();
		}

		constexpr std::size_t
			CHUNK_SIZE_INDEX   = 0,
			DEFAULT_SIZE_DIFF  = 3,
			CHUNK_START_INDEX  = 4,
			MASTODON_SIZE_DIFF = 9,
			DEFAULT_INDEX_DIFF = 12;

		const std::size_t
			PROFILE_VEC_DEFLATE_SIZE = profile_vec.size(),
			IMAGE_VEC_SIZE 			 = png_vec.size(),
			CHUNK_SIZE 				 = hasMastodonOption ? mastodon_deflate_size + MASTODON_SIZE_DIFF : PROFILE_VEC_DEFLATE_SIZE + DEFAULT_SIZE_DIFF,
			CHUNK_INDEX 			 = hasMastodonOption ? 0x21 : IMAGE_VEC_SIZE - DEFAULT_INDEX_DIFF,
			PROFILE_VEC_INDEX 		 = hasMastodonOption ? 0x0D : 0x0B;

		if (hasMastodonOption) {
			constexpr std::size_t
				MASTODON_ICCP_CHUNK_SIZE_DIFF = 5,
				TWITTER_ICCP_MAX_CHUNK_SIZE   = 10 * 1024,
				TWITTER_IMAGE_MAX_SIZE 		  = 5  * 1024 * 1024;

			const std::size_t MASTODON_CHUNK_SIZE = mastodon_deflate_size + MASTODON_ICCP_CHUNK_SIZE_DIFF;

			vBytes iccp_vec = { 0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
			iccp_vec.reserve(iccp_vec.size() + PROFILE_VEC_DEFLATE_SIZE + CHUNK_SIZE);

			iccp_vec.insert((iccp_vec.begin() + PROFILE_VEC_INDEX), profile_vec.begin(), profile_vec.end());
			updateValue(iccp_vec, CHUNK_SIZE_INDEX, MASTODON_CHUNK_SIZE);

			const std::size_t
				ICCP_CHUNK_CRC = lodepng_crc32(&iccp_vec[CHUNK_START_INDEX], CHUNK_SIZE),
				ICCP_CRC_INDEX = CHUNK_START_INDEX + CHUNK_SIZE;

			updateValue(iccp_vec, ICCP_CRC_INDEX, ICCP_CHUNK_CRC);
			png_vec.insert((png_vec.begin() + CHUNK_INDEX), iccp_vec.begin(), iccp_vec.end());

			vBytes().swap(iccp_vec);

			if (!hasTwitterBadDims && TWITTER_IMAGE_MAX_SIZE >= png_vec.size() && TWITTER_ICCP_MAX_CHUNK_SIZE >= MASTODON_CHUNK_SIZE) {
				platforms_vec[0] = std::move(platforms_vec[2]);
			} else {
				platforms_vec[0] = std::move(platforms_vec[3]);
			}
				platforms_vec.resize(1);
		} else {
			constexpr std::size_t
				IDAT_CHUNK_SIZE_DIFF 	  = 4,
				IDAT_CHUNK_CRC_INDEX_DIFF = 8;

			vBytes idat_vec = { 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54, 0x78, 0x5E, 0x5C, 0x00, 0x00, 0x00, 0x00 };
			idat_vec.reserve(idat_vec.size() + profile_vec.size() + CHUNK_SIZE);

			idat_vec.insert(idat_vec.begin() + PROFILE_VEC_INDEX, profile_vec.begin(), profile_vec.end());
			updateValue(idat_vec, CHUNK_SIZE_INDEX, CHUNK_SIZE);

			const std::size_t
				IDAT_CHUNK_CRC = lodepng_crc32(&idat_vec[CHUNK_START_INDEX], CHUNK_SIZE + IDAT_CHUNK_SIZE_DIFF),
				IDAT_CRC_INDEX = CHUNK_SIZE + IDAT_CHUNK_CRC_INDEX_DIFF;

			updateValue(idat_vec, IDAT_CRC_INDEX, IDAT_CHUNK_CRC);
			png_vec.insert((png_vec.begin() + CHUNK_INDEX), idat_vec.begin(), idat_vec.end());

			vBytes().swap(idat_vec);
		}

		vBytes().swap(profile_vec);

		const uint32_t RAND_NUM = 100000 + randombytes_uniform(900000);
		const std::string OUTPUT_FILENAME = std::format("prdt_{}.png", RAND_NUM);

		std::ofstream file_ofs(OUTPUT_FILENAME, std::ios::binary);

		if (!file_ofs) {
			throw std::runtime_error("Write Error: Unable to write to file.");
		}

		const std::size_t OUTPUT_SIZE = png_vec.size();

		file_ofs.write(reinterpret_cast<const char*>(png_vec.data()), OUTPUT_SIZE);
		file_ofs.close();

		if (hasNoOption) {
			platforms_vec.erase(platforms_vec.begin() + 2, platforms_vec.begin() + 5);

			constexpr std::size_t
				FLICKR_MAX_IMAGE_SIZE 			= 200 * 1024 * 1024,
				IMGBB_POSTIMAGE_MAX_IMAGE_SIZE 	= 32  * 1024 * 1024,
				IMGPILE_MAX_IMAGE_SIZE 			= 8   * 1024 * 1024,
				TWITTER_MAX_IMAGE_SIZE 			= 5   * 1024 * 1024;

			std::erase_if(platforms_vec, [&](const std::string& platform) {
				if ((platform == "X-Twitter" && OUTPUT_SIZE > TWITTER_MAX_IMAGE_SIZE) || (platform == "X-Twitter" && hasTwitterBadDims)) {
					return true;
				}
				if ((platform == "ImgBB" || platform == "PostImage") && (OUTPUT_SIZE > IMGBB_POSTIMAGE_MAX_IMAGE_SIZE)) {
					return true;
				}
				if (platform == "ImgPile" && OUTPUT_SIZE > IMGPILE_MAX_IMAGE_SIZE) {
					return true;
				}
				if (platform == "Flickr" && OUTPUT_SIZE > FLICKR_MAX_IMAGE_SIZE) {
					return true;
				}
				return false;
			});

			if (platforms_vec.empty()) {
				platforms_vec.emplace_back("\b\bUnknown!\n\n Due to the large file size of the output PNG image, I'm unaware of any\n compatible platforms that this image can be posted on. Local use only?");
			}
		}

		std::println("\nPlatform compatibility for output image:-\n");

		for (const auto& s : platforms_vec) {
			std::println(" ✓ {}", s);
		}

		std::println("\nSaved \"file-embedded\" PNG image: {} ({} bytes).", OUTPUT_FILENAME, OUTPUT_SIZE);
		std::println("\nRecovery PIN: [***{}***]\n\nImportant: Keep your PIN safe, so that you can extract the hidden file.\n\nComplete!\n", PIN);

		return 0;
	}

	int recoverData(vBytes& png_vec, Mode mode, Option option, fs::path& image_file_path) {
		constexpr auto
			ICCP_SIG = std::to_array<Byte>({ 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63 }),
			PDV_SIG  = std::to_array<Byte>({ 0xC6, 0x50, 0x3C, 0xEA, 0x5E, 0x9D, 0xF9 });

		constexpr std::size_t ICCP_CHUNK_INDEX = 0x25;

		auto
			iccp_chunk_opt = searchSig(png_vec, ICCP_SIG),
			pdv_chunk_opt  = searchSig(png_vec, PDV_SIG);

		std::size_t iccp_chunk_index = *iccp_chunk_opt;

		if (iccp_chunk_index != ICCP_CHUNK_INDEX && (!pdv_chunk_opt)) {
			throw std::runtime_error("Image File Error: This is not a pdvrdt image.");
		}

		bool isMastodonFile = (iccp_chunk_index == ICCP_CHUNK_INDEX && (!pdv_chunk_opt));

		constexpr std::size_t
			DEFAULT_CHUNK_SIZE_INDEX_DIFF 	 = 112,
			DEFAULT_PROFILE_DATA_INDEX_DIFF  = 11,
			MASTODON_PROFILE_DATA_INDEX_DIFF = 9,
			MASTODON_CHUNK_SIZE_DIFF 	 	 = 9,
			MASTODON_CHUNK_SIZE_INDEX_DIFF 	 = 4,
			CHUNK_SIZE_DIFF 		 		 = 3;

		#if defined(_MSC_VER)
			__analysis_assume(pdv_chunk_opt.has_value());
		#endif

		std::size_t pdv_chunk_index = *pdv_chunk_opt;

		const std::size_t
			CHUNK_SIZE_INDEX = isMastodonFile
				? iccp_chunk_index - MASTODON_CHUNK_SIZE_INDEX_DIFF
				: pdv_chunk_index - DEFAULT_CHUNK_SIZE_INDEX_DIFF,
			PROFILE_DATA_INDEX = isMastodonFile
				? iccp_chunk_index + MASTODON_PROFILE_DATA_INDEX_DIFF
				: CHUNK_SIZE_INDEX + DEFAULT_PROFILE_DATA_INDEX_DIFF,
			CHUNK_SIZE = isMastodonFile
				? getValue(png_vec, CHUNK_SIZE_INDEX) - MASTODON_CHUNK_SIZE_DIFF
				: getValue(png_vec, CHUNK_SIZE_INDEX);

		Byte byte = png_vec.back();

		png_vec.erase(png_vec.begin(), png_vec.begin() + PROFILE_DATA_INDEX);
		png_vec.erase(png_vec.begin() + (isMastodonFile ? CHUNK_SIZE + CHUNK_SIZE_DIFF : CHUNK_SIZE - CHUNK_SIZE_DIFF), png_vec.end());

		if (isMastodonFile) {
			zlibFunc(png_vec, mode, option);
			if (png_vec.empty()) {
				throw std::runtime_error("File Size Error: File is zero bytes. Probable failure inflating file.");
			}
			auto pdv_iccp_chunk_opt = searchSig(png_vec, PDV_SIG);

			if (!pdv_iccp_chunk_opt) {
				throw std::runtime_error("Image File Error: This is not a pdvrdt image.");
			}
		}

		bool hasDecryptionFailed = false;

		std::string decrypted_filename = decryptDataFile(png_vec, isMastodonFile, hasDecryptionFailed);

		if (hasDecryptionFailed) {
			std::fstream file(image_file_path, std::ios::in | std::ios::out | std::ios::binary);

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
				std::ofstream file(image_file_path, std::ios::out | std::ios::trunc | std::ios::binary);
			} else {
				file.seekp(byte_pos);
				file.write(reinterpret_cast<char*>(&byte), sizeof(byte));
			}
			file.close();
			throw std::runtime_error("File Recovery Error: Invalid PIN or file is corrupt.");
		}

		zlibFunc(png_vec, mode, option);

		const std::size_t INFLATED_FILE_SIZE = png_vec.size();

		if (!INFLATED_FILE_SIZE) {
			throw std::runtime_error("Zlib Compression Error: Output file is empty. Inflating file failed.");
		}

		if (byte != 0x82) {
			std::fstream file(image_file_path, std::ios::in | std::ios::out | std::ios::binary);

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

		std::println("\nExtracted hidden file: {} ({} bytes).\n\nComplete! Please check your file.\n", decrypted_filename, INFLATED_FILE_SIZE);

		return 0;
	}
}

int main(int argc, char** argv) {
	try {
		 if (sodium_init() < 0) {
            throw std::runtime_error("Libsodium initialization failed!");
        }

		#ifdef _WIN32
    		SetConsoleOutputCP(CP_UTF8);  
		#endif
	
		auto args_opt = ProgramArgs::parse(argc, argv);
        if (!args_opt) return 0;

        ProgramArgs args = *args_opt;

        bool isConcealMode = (args.mode == Mode::conceal);

        vBytes png_vec = readFile(args.image_file_path, isConcealMode ? FileTypeCheck::cover_image : FileTypeCheck::embedded_image);

        if (isConcealMode) {
            concealData(png_vec, args.mode, args.option, args.data_file_path);
        } else {
            recoverData(png_vec, args.mode, args.option, args.image_file_path);
        }
    }
    catch (const std::runtime_error& e) {
        std::println(std::cerr, "\n{}\n", e.what());
        return 1;
    }
     return 0;
}

