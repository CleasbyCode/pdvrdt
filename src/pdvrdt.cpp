//	PNG Data Vehicle (pdvrdt v1.8). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023
// 
//	Compile program (Linux)
//	$ g++ pdvrdt.cpp -O2 -lz -s -o pdvrdt
// 
//	Run it
//	$ ./pdvrdt

#include <algorithm>
#include <fstream>
#include <filesystem>	
#include <iostream>
#include <regex>
#include <string>
#include <cstdint>
#include <vector>
#include <zlib.h>

struct PDV_STRUCT {
	const size_t MAX_FILE_SIZE = 209715200, MAX_FILE_SIZE_REDDIT = 19922944, MAX_FILE_SIZE_MASTODON = 16777216;
	std::vector<unsigned char> Image_Vec, File_Vec, Profile_Data_Vec;
	std::string image_file_name, data_file_name;
	size_t image_size{}, data_size{};
	bool embed_file_mode = false, mastodon_opt = false, reddit_opt = false;
};

void
	// Attempt to open image file, followed by some basic checks to make sure user's image file meets program requirements.
	Check_Image_File(PDV_STRUCT&),
	// Attempt to open data file, followed by some basic checks to make sure user's data file meets program requirements.
	Check_Data_File(PDV_STRUCT&),
	// Fill vector with our ICC Profile data & initialise other vectors.
	Fill_Profile_Vec(PDV_STRUCT&),
	// Start the data file extraction process.
	Extract_Data_File(PDV_STRUCT&),
	// Encrypt or decrypt user's data file, depending on mode.
	Encrypt_Decrypt(PDV_STRUCT& pdv),
	// Search and remove all unnecessary PNG chunks.
	Erase_Chunks(PDV_STRUCT& pdv),
	// Depending on mode (embed/extract) Inflate or Deflate the IDAT chunk (or iCCP chunk if mastodon option was used), which contains user's encrypted data file.
	Inflate_Deflate(std::vector<unsigned char>&, bool),
	// Write out to file the file-embedded image or the extracted data file.
	Write_Out_File(PDV_STRUCT& pdv),
	// Update values, such as chunk lengths, CRC, file sizes, etc. Writes them into the relevant vector index locations.
	Value_Updater(std::vector<unsigned char>&, size_t, const size_t&, uint_fast8_t),
	// Check args input for invalid data.
	Check_Arguments_Input(const std::string&),
	// Display program information.
	Display_Info();

// Code to compute CRC32 (for iCCP/IDAT/PLTE chunks within this program).  https://www.w3.org/TR/2003/REC-PNG-20031110/#D-CRCAppendix 
uint_fast64_t
	Crc_Update(const uint_fast64_t&, unsigned char*, const uint_fast64_t&),
	Crc(unsigned char*, const uint_fast64_t&);

int main(int argc, char** argv) {

	PDV_STRUCT pdv;

	if (argc == 2 && std::string(argv[1]) == "--info") {
		Display_Info();
	}
	else if (argc == 3 && std::string(argv[1]) == "-x") {

		// Extract file mode.
		pdv.image_file_name = argv[2];

		Check_Image_File(pdv);
	}
	else if (argc >= 4 && argc < 6 && std::string(argv[1]) == "-e") {
		// Embed file mode.
		if (argc == 4 && (std::string(argv[2]) == "-m" || std::string(argv[2]) == "-r")) {
			std::cerr << "\nFile Error: Missing argument.\n\n";
			std::exit(EXIT_FAILURE);
		}

		pdv.embed_file_mode = true;

		if (argc == 5 && std::string(argv[2]) == "-m") {
			pdv.mastodon_opt = true;
		}
		if (argc == 5 && std::string(argv[2]) == "-r") {
			pdv.reddit_opt = true;
		}

		pdv.image_file_name = pdv.mastodon_opt || pdv.reddit_opt ? argv[3] : argv[2];
		pdv.data_file_name = pdv.mastodon_opt || pdv.reddit_opt ? argv[4] : argv[3];

		Check_Image_File(pdv);
	}
	else {
		std::cout << "\nUsage:\tpdvrdt -e [-m] [-r] <cover_image> <data_file>\n\tpdvrdt -x <file_embedded_image>\n\tpdvrdt --info\n\n";
	}
	return 0;
}

void Check_Image_File(PDV_STRUCT& pdv) {

	Check_Arguments_Input(pdv.image_file_name);

        const std::string IMG_EXTENSION = pdv.image_file_name.length() > 2 ? pdv.image_file_name.substr(pdv.image_file_name.length() - 3) : pdv.image_file_name;

        std::ifstream read_image_fs(pdv.image_file_name, std::ios::binary);

        if (!read_image_fs || (IMG_EXTENSION !="png" && IMG_EXTENSION !="jpg")) {
                std::cerr << (IMG_EXTENSION != "png" ? "\nImage File Error: Invalid file extension. Only expecting 'png' for image file"
                : "\nRead File Error: Unable to open image file") << ".\n\n";
                std::exit(EXIT_FAILURE);
        }

        // Check PNG image for valid file size requirements.
        pdv.image_size = std::filesystem::file_size(pdv.image_file_name);

        constexpr uint_fast8_t PNG_MIN_SIZE = 68;

	if (pdv.image_size > pdv.MAX_FILE_SIZE
		|| pdv.mastodon_opt && pdv.image_size > pdv.MAX_FILE_SIZE_MASTODON
		|| pdv.reddit_opt && pdv.image_size > pdv.MAX_FILE_SIZE_REDDIT
		|| PNG_MIN_SIZE > pdv.image_size) {
		// Image size is too small or larger than the set size limits. Display relevant error message and exit program.

		std::cerr << "\nImage File Error: " << (PNG_MIN_SIZE > pdv.image_size ? "Size of image is too small to be a valid PNG image"
			: "Size of image exceeds the maximum limit of " + (pdv.mastodon_opt ? std::to_string(pdv.MAX_FILE_SIZE_MASTODON) + " Bytes"
				: (pdv.reddit_opt ? std::to_string(pdv.MAX_FILE_SIZE_REDDIT)
					: std::to_string(pdv.MAX_FILE_SIZE)) + " Bytes")) << ".\n\n";
		std::exit(EXIT_FAILURE);
	}

	std::string option = pdv.mastodon_opt ? "-m" : "-r";

	// Display Start message. Different depending on mode and options selected.
	std::cout << (pdv.mastodon_opt || pdv.reddit_opt ? "\nEmbed mode selected with " + option + " option.\n\nReading files"
		: (pdv.embed_file_mode ? "\nEmbed mode selected.\n\nReading files"
			: "\neXtract mode selected.\n\nReading PNG image file")) << ". Please wait...\n";

	// Read PNG image (embedded or non-embedded) into vector "Image_Vec".
	pdv.Image_Vec.assign(std::istreambuf_iterator<char>(read_image_fs), std::istreambuf_iterator<char>());

	// Update image size variable with vector size storing user's image file.
	pdv.image_size = pdv.Image_Vec.size();

	const std::string
		PNG_HEADER_SIG = "\x89\x50\x4E\x47", // PNG image header signature. 
		PNG_END_SIG = "\x49\x45\x4E\x44\xAE\x42\x60\x82", // PNG image end signature.
		GET_PNG_HEADER_SIG{ pdv.Image_Vec.begin(), pdv.Image_Vec.begin() + PNG_HEADER_SIG.length() },	// Attempt to get both image signatures from file stored in vector. 
		GET_PNG_END_SIG{ pdv.Image_Vec.end() - PNG_END_SIG.length(), pdv.Image_Vec.end() };

	// Make sure image has valid PNG signatures.
	if (GET_PNG_HEADER_SIG != PNG_HEADER_SIG || GET_PNG_END_SIG != PNG_END_SIG) {

		// File requirements check failure, display relevant error message and exit program.
		std::cerr << "\nImage File Error: File does not appear to be a valid PNG image.\n\n";
		std::exit(EXIT_FAILURE);
	}

	if (pdv.embed_file_mode) {
		// Remove unwanted PNG chunks.
		Erase_Chunks(pdv);
	}
	else {
		Extract_Data_File(pdv);
	}
}

void Check_Data_File(PDV_STRUCT& pdv) {

	Check_Arguments_Input(pdv.data_file_name);

	std::ifstream read_file_fs(pdv.data_file_name, std::ios::binary);

	// Make sure data file opened successfully.
	if (!read_file_fs) {
		// Open file failure, display relevant error message and exit program.
		std::cerr << "\nRead File Error: Unable to open data file.\n\n";
		std::exit(EXIT_FAILURE);
	}

	pdv.data_size = std::filesystem::file_size(pdv.data_file_name);

	const size_t LAST_SLASH_POS = pdv.data_file_name.find_last_of("\\/");

	// Check for and remove "./" or ".\" characters at the start of the file name. 
	if (LAST_SLASH_POS <= pdv.data_file_name.length()) {
		const std::string_view NO_SLASH_NAME(pdv.data_file_name.c_str() + (LAST_SLASH_POS + 1), pdv.data_file_name.length() - (LAST_SLASH_POS + 1));
		pdv.data_file_name = NO_SLASH_NAME;
	}

	const size_t FILE_NAME_LENGTH = pdv.data_file_name.length();

	// Check file size and length of file name.
	constexpr uint_fast8_t MAX_FILENAME_LENGTH = 23;

	if (pdv.data_size > pdv.MAX_FILE_SIZE
		|| pdv.mastodon_opt && pdv.data_size > pdv.MAX_FILE_SIZE_MASTODON
		|| pdv.reddit_opt && pdv.data_size > pdv.MAX_FILE_SIZE_REDDIT
		|| FILE_NAME_LENGTH > pdv.data_size
		|| FILE_NAME_LENGTH > MAX_FILENAME_LENGTH) {
		// File name too long, or image size is too small or larger than size limit. Display relevant error message and exit program.
		std::cerr << "\nData File Error: " << (FILE_NAME_LENGTH > MAX_FILENAME_LENGTH ? "Length of file name is too long.\n\nFor compatibility requirements, length of file name must be under 24 characters"
			: (FILE_NAME_LENGTH > pdv.data_size ? "Size of file is too small.\n\nFor compatibility requirements, file size must be greater than the length of the file name"
				: "Size of file exceeds the maximum limit of " + (pdv.mastodon_opt ? std::to_string(pdv.MAX_FILE_SIZE_MASTODON) + " Bytes"
					: (pdv.reddit_opt ? std::to_string(pdv.MAX_FILE_SIZE_REDDIT)
						: std::to_string(pdv.MAX_FILE_SIZE)) + " Bytes"))) << ".\n\n";
		std::exit(EXIT_FAILURE);
	}

	// Read-in user's data file and store it in vector "File_Vec".
	pdv.File_Vec.assign(std::istreambuf_iterator<char>(read_file_fs), std::istreambuf_iterator<char>());

	// Update size variable of the user's data file stored in vector.
	pdv.data_size = pdv.File_Vec.size();

	// Combined file size check.
	if (pdv.image_size + pdv.data_size > pdv.MAX_FILE_SIZE
		|| pdv.mastodon_opt && pdv.image_size + pdv.data_size > pdv.MAX_FILE_SIZE_MASTODON
		|| pdv.reddit_opt && pdv.image_size + pdv.data_size > pdv.MAX_FILE_SIZE_REDDIT) {
		// File size check failure, display relevant error message and exit program.

		std::cerr << "\nFile Size Error: The combined file size of the PNG image & data file, must not exceed "
			<< (pdv.mastodon_opt ? std::to_string(pdv.MAX_FILE_SIZE_MASTODON)
				: (pdv.reddit_opt ? std::to_string(pdv.MAX_FILE_SIZE_REDDIT)
					: std::to_string(pdv.MAX_FILE_SIZE))) << " Bytes.\n\n";
		std::exit(EXIT_FAILURE);
	}

	Fill_Profile_Vec(pdv);
}

void Fill_Profile_Vec(PDV_STRUCT& pdv) {

	// The first 401 bytes of the vector "Profile_Data_Vec" contains the basic ICC profile, followed by a 3-byte signature "PDV". 404 bytes total.
	// The length value of the user's data file name and the file name (encrypted) will also be stored within this profile.
	// After the user's data file has been encrypted and compressed, it will be inserted and stored at the end of this profile.

	pdv.Profile_Data_Vec={
		0x00, 0x00, 0x00, 0x00, 0x6c, 0x63, 0x6d, 0x73, 0x02, 0x10, 0x00, 0x00, 0x6D, 0x6E, 0x74,
		0x72, 0x52, 0x47, 0x42, 0x20, 0x58, 0x59, 0x5A, 0x20, 0x07, 0xE2, 0x00, 0x03, 0x00, 0x14,
		0x00, 0x09, 0x00, 0x0E, 0x00, 0x1D, 0x61, 0x63, 0x73, 0x70, 0x4D, 0x53, 0x46, 0x54, 0x00,
		0x00, 0x00, 0x00, 0x73, 0x61, 0x77, 0x73, 0x63, 0x74, 0x72, 0x6C, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF6, 0xD6, 0x00, 0x01, 0x00,
		0x00, 0x00, 0x00, 0xD3, 0x2D, 0x68, 0x61, 0x6E, 0x64, 0xEB, 0x77, 0x1F, 0x3C, 0xAA, 0x53,
		0x51, 0x02, 0xE9, 0x3E, 0x28, 0x6C, 0x91, 0x46, 0xAE, 0x57, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x64, 0x65, 0x73,
		0x63, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x1C, 0x77, 0x74, 0x70, 0x74, 0x00, 0x00,
		0x01, 0x0C, 0x00, 0x00, 0x00, 0x14, 0x72, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x20, 0x00,
		0x00, 0x00, 0x14, 0x67, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x34, 0x00, 0x00, 0x00, 0x14,
		0x62, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x48, 0x00, 0x00, 0x00, 0x14, 0x72, 0x54, 0x52,
		0x43, 0x00, 0x00, 0x01, 0x5C, 0x00, 0x00, 0x00, 0x34, 0x67, 0x54, 0x52, 0x43, 0x00, 0x00,
		0x01, 0x5C, 0x00, 0x00, 0x00, 0x34, 0x62, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0x5C, 0x00,
		0x00, 0x00, 0x34, 0x63, 0x70, 0x72, 0x74, 0x00, 0x00, 0x01, 0x90, 0x00, 0x00, 0x00, 0x01,
		0x64, 0x65, 0x73, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x6E, 0x52, 0x47,
		0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0x59,
		0x5A, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x54, 0x00, 0x01, 0x00, 0x00, 0x00,
		0x01, 0x16, 0xC9, 0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6F, 0xA0,
		0x00, 0x00, 0x38, 0xF2, 0x00, 0x00, 0x03, 0x8F, 0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x62, 0x96, 0x00, 0x00, 0xB7, 0x89, 0x00, 0x00, 0x18, 0xDA, 0x58, 0x59,
		0x5A, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0xA0, 0x00, 0x00, 0x0F, 0x85, 0x00,
		0x00, 0xB6, 0xC4, 0x63, 0x75, 0x72, 0x76, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14,
		0x00, 0x00, 0x01, 0x07, 0x02, 0xB5, 0x05, 0x6B, 0x09, 0x36, 0x0E, 0x50, 0x14, 0xB1, 0x1C,
		0x80, 0x25, 0xC8, 0x30, 0xA1, 0x3D, 0x19, 0x4B, 0x40, 0x5B, 0x27, 0x6C, 0xDB, 0x80, 0x6B,
		0x95, 0xE3, 0xAD, 0x50, 0xC6, 0xC2, 0xE2, 0x31, 0xFF, 0xFF, 0x00, 0x50, 0x44, 0x56
	};

	if (pdv.reddit_opt) {

		// There are two IDAT chunks when the Reddit option (-r) is selected.
		// The first chunk is filled with 512KB of '0'. 
		// The second IDAT chunk is the user's compressed/encrypted data file.
		// Reddit moves both chunks below the IEND chunk (when the image is saved/downloaded).
		// The first chunk is often partitially erased, that is why we use the first IDAT chunk filled with '0',
		// to protect the second chunk containing our data file.

		std::vector<unsigned char>Idat_Reddit_Vec = { 0x00, 0x08, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54 };

		constexpr unsigned char Idat_Reddit_Crc[4]{ 0xA3, 0x1A, 0x50, 0xFA };

		std::fill_n(std::back_inserter(Idat_Reddit_Vec), 0x80000, 0);

		Idat_Reddit_Vec.insert(Idat_Reddit_Vec.end(), &Idat_Reddit_Crc[0], &Idat_Reddit_Crc[4]);
		pdv.Image_Vec.insert(pdv.Image_Vec.end() - 12, Idat_Reddit_Vec.begin(), Idat_Reddit_Vec.end());
	}
	std::cout << "\nEncrypting data file.\n";

	// Encrypt the user's data file and its file name.
	Encrypt_Decrypt(pdv);
}

void Extract_Data_File(PDV_STRUCT& pdv) {

	// Update data file size variable.
	pdv.data_size = pdv.Image_Vec.size();

	constexpr uint_fast8_t ICCP_CHUNK_INDEX = 33;	// ICCP chunk location within the embedded PNG image file (Only exists if -m (mastodon) option was used when image was embedded). 

	constexpr unsigned char SIG[14]{ 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63, 0x49, 0x44, 0x41, 0x54, 0x5F, 0x78, 0x9c }; // Vector contains signature for ICCP (iCCPicc) and IDAT (IDAT0x780x9C) chunks.

	// Search image file for these signatures, to make sure a valid file-embedded chunk exists. 
	const auto
		ICCP_POS = std::search(pdv.Image_Vec.begin(), pdv.Image_Vec.end(), &SIG[0], &SIG[6]) - pdv.Image_Vec.begin() - 4,
		IDAT_POS = std::search(pdv.Image_Vec.begin(), pdv.Image_Vec.end(), &SIG[7], &SIG[13]) - pdv.Image_Vec.begin() - 4;

	if (ICCP_POS != ICCP_CHUNK_INDEX && IDAT_POS == pdv.Image_Vec.size() - 4) {
		// ICCP chunk not found or not found in the correct location. / IDAT chunk not found.
		// Requirement checks failure, display relevant error message and exit program.
		std::cerr << "\nImage File Error: This is not a pdvrdt file-embedded image.\n\n";
		std::exit(EXIT_FAILURE);
	}

	if (ICCP_POS == ICCP_CHUNK_INDEX) {
		pdv.mastodon_opt = true;  // Found a ICCP chunk. Image was embedded with file, using the -m Mastodon option. Enable it here to make selection choices later.
	}

	std::cout << "\nFound compressed data chunk.\n";

	const size_t
		CHUNK_SIZE_INDEX = pdv.mastodon_opt ? ICCP_POS : IDAT_POS,		// If -m Mastodon option. Start index location of 4 byte iCCP Profile chunk length field, else index location of (last) IDAT chunk length field.
		DEFLATE_DATA_INDEX = pdv.mastodon_opt ? ICCP_POS + 13 : IDAT_POS + 9,	// Start index location of deflate data (0x78,0x9C...) within iCCP chunk (if -m mastodon option) or last IDAT chunk.
		// Get ICCP chunk length (if -m mastodon option) or last IDAT chunk length.
		CHUNK_SIZE = ((static_cast<size_t>(pdv.Image_Vec[CHUNK_SIZE_INDEX]) << 24)
			| (static_cast<size_t>(pdv.Image_Vec[CHUNK_SIZE_INDEX + 1]) << 16)
			| (static_cast<size_t>(pdv.Image_Vec[CHUNK_SIZE_INDEX + 2]) << 8)
			| (static_cast<size_t>(pdv.Image_Vec[CHUNK_SIZE_INDEX + 3]))),

		DEFLATE_CHUNK_SIZE = pdv.mastodon_opt ? CHUNK_SIZE - 9 : CHUNK_SIZE;

	// From "Image_Vec" vector index 0, erase n bytes so that the start of the vector's contents is now the beginning of the deflate data (0x78, 0x9C...).
	pdv.Image_Vec.erase(pdv.Image_Vec.begin(), pdv.Image_Vec.begin() + DEFLATE_DATA_INDEX);

	// Erase all bytes of "Image_Vec" after the end of the deflate data. Vector should now just contain the deflate data.
	pdv.Image_Vec.erase(pdv.Image_Vec.begin() + DEFLATE_CHUNK_SIZE, pdv.Image_Vec.end());

	std::cout << "\nInflating data.\n";

	// Call function to inflate the data chunk, which includes user's encrypted data file. 
	Inflate_Deflate(pdv.Image_Vec, pdv.embed_file_mode);

	// A zero byte "Image_Vec" vector indicates that the zlib inflate function failed.
	if (!pdv.Image_Vec.size()) {
		std::cerr << "\nImage File Error: Inflating data chunk failed. Not a pdvrdt file-embedded-image or image file is corrupt.\n\n";
		std::exit(EXIT_FAILURE);
	}

	const uint_fast8_t FILENAME_LENGTH = pdv.Image_Vec[100];	// From vector index location, get stored length value of the embedded file name of user's data file.
	
	constexpr uint_fast16_t
		FILENAME_START_INDEX = 101,		// Vector index start location of the embedded file name of user's data file.
		PDV_SIG_START_INDEX = 401,	// Vector index for this program's embedded signature.
		DATA_FILE_START_INDEX = 404;	// Vector index start location for user's embedded data file.

	// Get encrypted embedded file name from vector "Image_Vec".
	pdv.data_file_name = { pdv.Image_Vec.begin() + FILENAME_START_INDEX, pdv.Image_Vec.begin() + FILENAME_START_INDEX + FILENAME_LENGTH };

	const std::string 
		PDV_SIG = "PDV",	// pdvrdt signature.
		GET_PDV_SIG{ pdv.Image_Vec.begin() + PDV_SIG_START_INDEX, pdv.Image_Vec.begin() + PDV_SIG_START_INDEX + PDV_SIG.length() }; // Attempt to get the pdvrdt signature from vector "Image_Vec".

	// Make sure this is a pdvrdt file-embedded image.
	if (GET_PDV_SIG != PDV_SIG) {
		std::cerr << "\nImage File Error: Signature match failure. This is not a pdvrdt file-embedded image.\n\n";
		std::exit(EXIT_FAILURE);
	}

	std::cout << "\nFound pdvrdt signature.\n\nExtracting encrypted data file from PNG image.\n";

	// Delete the profile from the inflated file, leaving just our encrypted data file.
	pdv.Image_Vec.erase(pdv.Image_Vec.begin(), pdv.Image_Vec.begin() + DATA_FILE_START_INDEX);

	std::cout << "\nDecrypting data file.\n";

	// Decrypt the contents of vector "Image_Vec".
	Encrypt_Decrypt(pdv);
}

void Encrypt_Decrypt(PDV_STRUCT& pdv) {

	const std::string
		XOR_KEY = "\xFC\xD8\xF9\xE2\x9F\x7E",	// String used to XOR encrypt/decrypt the file name of user's data file.
		INPUT_NAME = pdv.data_file_name;

	std::string output_name;

	size_t
		file_size = pdv.embed_file_mode ? pdv.File_Vec.size() : pdv.Image_Vec.size(),	 // File size of user's data file.
		index_pos = 0;		// When encrypting/decrypting the file name, this variable stores the index character position of the file name,
					// When encrypting/decrypting the user's data file, this variable is used as the index position of where to 
					// insert each byte of the data file into the relevant vectors.
	uint_fast8_t
		xor_key_pos = 0,	// Character position variable for XOR_KEY string.
		name_key_pos = 0;	// Character position variable for file name string (output_name / INPUT_NAME).

	// XOR encrypt or decrypt file name and data file.
	while (file_size > index_pos) {

		if (index_pos >= INPUT_NAME.length()) {
			name_key_pos = name_key_pos > INPUT_NAME.length() ? 0 : name_key_pos;	 // Reset file name character position to the start if it has reached last character.
		}
		else {
			xor_key_pos = xor_key_pos > XOR_KEY.length() ? 0 : xor_key_pos;		// Reset XOR_KEY position to the start if it has reached the last character.
			output_name += INPUT_NAME[index_pos] ^ XOR_KEY[xor_key_pos++];		// XOR each character of the file name against characters of XOR_KEY string. Store output characters in "output_name".
												// Depending on mode, file name is either encrypted or decrypted.
		}

		// Encrypt data file. XOR each byte of the data file within vector "File_Vec" against each character of the encrypted file name, "output_name". 
		// Store encrypted output in vector "Profile_Data_Vec".
		// Decrypt data file: XOR each byte of the data file within vector "Image_Vec" against each character of the encrypted file name, "INPUT_NAME". 
		// Store decrypted output in vector "Decrypted_Vec".
		pdv.embed_file_mode ? pdv.Profile_Data_Vec.emplace_back(pdv.File_Vec[index_pos++] ^ output_name[name_key_pos++])
			: pdv.File_Vec.emplace_back(pdv.Image_Vec[index_pos++] ^ INPUT_NAME[name_key_pos++]);
	}

	if (pdv.embed_file_mode) {

		constexpr uint_fast8_t
			PROFILE_NAME_LENGTH_INDEX = 100,	// Index location inside the compressed profile to store the length value of the user's data file name (1 byte).
			PROFILE_NAME_INDEX = 101;		// Start index location inside the compressed profile to store the encrypted file name for the user's embedded file.

		// Insert the character length value of the file name into the profile.
		// We need to know how many characters to read when we later retrieve the file name from the profile, during file extraction.
		pdv.Profile_Data_Vec[PROFILE_NAME_LENGTH_INDEX] = static_cast<unsigned char>(INPUT_NAME.length());

		// Insert the encrypted filename into the profile.
		std::copy(output_name.begin(), output_name.end(), pdv.Profile_Data_Vec.begin() + PROFILE_NAME_INDEX);

		uint_fast8_t
			profile_data_vec_size_index = 0,	// Start index location of the compressed profile's 4 byte length field.
			chunk_size_index = 0,			// Start index of the last IDAT chunk's 4 byte length field, (or iCCP chunk's length field, if -m mastodon option used).
			bits = 32;				// Value used in the "Value_Update" function. 4 bytes.

		// Update internal profile size.
		Value_Updater(pdv.Profile_Data_Vec, profile_data_vec_size_index, pdv.Profile_Data_Vec.size(), bits);

		std::cout << "\nCompressing data file.\n";

		// Call function to deflate the contents of vector "Profile_Data_Vec" (profile with user's encrypted file).
		Inflate_Deflate(pdv.Profile_Data_Vec, pdv.embed_file_mode);

		const size_t
			PROFILE_DEFLATE_SIZE = pdv.Profile_Data_Vec.size(),
			CHUNK_SIZE = pdv.mastodon_opt ? PROFILE_DEFLATE_SIZE + 9 : PROFILE_DEFLATE_SIZE + 5, // IDAT chunk (+ 4 bytes) or, if -m mastodon option used, "iCCPicc\x00\x00" (+ 9 bytes).
			CHUNK_INSERT_INDEX = pdv.mastodon_opt ? 33 : pdv.Image_Vec.size() - 12;

		const uint_fast8_t PROFILE_DATA_INSERT_INDEX = pdv.mastodon_opt ? 13 : 9;   // If -m mastodon option used, index pos within Iccp_Chunk_Vec (13) to insert deflate contents of Profile_Data_Vec, else use Idat_Chunk_Vec (8) where we insert Profile_Data_Vec.
		constexpr uint_fast8_t CHUNK_START_INDEX = 4; 	// Start index location of chunk is where the name type begins (IDAT or iCCP). This start location is required when we get the CRC value.

		std::cout << "\nEmbedding data file within the PNG image.\n";

		if (pdv.mastodon_opt) {	// For -m Mastodon option, we embed the profile and data file within the iCCP chunk. This is for compatibility reasons. This is the only chunk that works for mastodon.

			std::vector<unsigned char>Iccp_Chunk_Vec = { 0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

			// Insert the encrypted/compressed iCCP chunk (with profile + user's data file) into vector "Iccp_Chunk_Vec".
			Iccp_Chunk_Vec.insert((Iccp_Chunk_Vec.begin() + PROFILE_DATA_INSERT_INDEX), pdv.Profile_Data_Vec.begin(), pdv.Profile_Data_Vec.end());

			// Call function to insert new chunk length value into "Iccp_Chunk_Vec" vector's index length field. 
			Value_Updater(Iccp_Chunk_Vec, chunk_size_index, PROFILE_DEFLATE_SIZE + 5, bits);

			// Pass these two values (CHUNK_START_INDEX and CHUNK_SIZE) to the CRC fuction to get correct iCCP chunk CRC value.
			const size_t ICCP_CHUNK_CRC = Crc(&Iccp_Chunk_Vec[CHUNK_START_INDEX], CHUNK_SIZE);

			// Index location for the 4-byte CRC field of the iCCP chunk.
			size_t iccp_crc_insert_index = CHUNK_START_INDEX + CHUNK_SIZE;

			// Call function to insert iCCP chunk CRC value into "Iccp_Chunk_Vec" vector's CRC index field. 
			Value_Updater(Iccp_Chunk_Vec, iccp_crc_insert_index, ICCP_CHUNK_CRC, bits);

			// Insert contents of vector "Iccp_Chunk_Vec" into vector "Image_Vec", combining iCCP chunk (profile + user's data file) within PNG image.
			pdv.Image_Vec.insert((pdv.Image_Vec.begin() + CHUNK_INSERT_INDEX), Iccp_Chunk_Vec.begin(), Iccp_Chunk_Vec.end());

		}
		else { // For all the other supported platforms, we embed the data file in a IDAT chunk (which will be the last IDAT chunk).

			std::vector<unsigned char>Idat_Chunk_Vec = { 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54, 0x5F, 0x00, 0x00, 0x00, 0x00 };

			// Insert the contents of vector Profile_Data_Vec (with user's encrypted/compressed data file) into vector "Idat_Chunk_Vec".
			Idat_Chunk_Vec.insert(Idat_Chunk_Vec.begin() + PROFILE_DATA_INSERT_INDEX, pdv.Profile_Data_Vec.begin(), pdv.Profile_Data_Vec.end());

			// Call function to insert new chunk length value into "Idat_Chunk_Vec" vector's index length field. 
			Value_Updater(Idat_Chunk_Vec, chunk_size_index, PROFILE_DEFLATE_SIZE + 1, bits);

			// Pass these two values (CHUNK_START_INDEX and CHUNK_SIZE) to the CRC fuction to get correct IDAT chunk CRC value.
			const size_t IDAT_CHUNK_CRC = Crc(&Idat_Chunk_Vec[CHUNK_START_INDEX], CHUNK_SIZE);

			// Index location for the 4-byte CRC field of the IDAT chunk.
			size_t idat_crc_insert_index = CHUNK_START_INDEX + CHUNK_SIZE;

			// Call function to insert IDAT chunk CRC value into "Idat_Chunk_Vec" vector's CRC index field. 
			Value_Updater(Idat_Chunk_Vec, idat_crc_insert_index, IDAT_CHUNK_CRC, bits);

			// Insert contents of vector "Idat_Chunk_Vec" into vector "Image_Vec", combining IDAT chunk (profile + user's data file) with PNG image.
			// The profile data is only required when using the iCCP chunk, but for now I have let it remain within the IDAT chunk as it does no harm, apart from waste 400 bytes.
			pdv.Image_Vec.insert((pdv.Image_Vec.begin() + CHUNK_INSERT_INDEX), Idat_Chunk_Vec.begin(), Idat_Chunk_Vec.end());
		}

		if (pdv.mastodon_opt && pdv.Image_Vec.size() > pdv.MAX_FILE_SIZE_MASTODON || pdv.reddit_opt && pdv.Image_Vec.size() > pdv.MAX_FILE_SIZE_REDDIT || pdv.Image_Vec.size() > pdv.MAX_FILE_SIZE) {
			std::cout << "\nImage Size Error: The file embedded image exceeds the maximum size of " << (pdv.mastodon_opt ? "16MB (Mastodon)."
				: (pdv.reddit_opt ? "19MB (Reddit)." : "200MB.")) << "\n\nYour data file has not been compressible and has had the opposite effect of making the file larger than the original. Perhaps try again with a smaller cover image.\n\n";
			std::exit(EXIT_FAILURE);
		}

		// Create a unique filename using time value for the complete output file. 
		srand((unsigned)time(NULL));  // For output filename.

		const std::string TIME_VALUE = std::to_string(rand());

		pdv.data_file_name = "prdt_" + TIME_VALUE.substr(0, 5) + ".png";

		std::cout << "\nWriting file-embedded PNG image out to disk.\n";

		// Write out to file the PNG image with the embedded (encrypted/compressed) user's data file.
		Write_Out_File(pdv);
	}
	else {
		// Update string variable with the decrypted file name.
		pdv.data_file_name = output_name;

		// Write the extracted (inflated / decrypted) data file out to disk.
		Write_Out_File(pdv);
	}
}

void Inflate_Deflate(std::vector<unsigned char>& Vec, bool embed_file_mode) {

	// zlib function, see https://zlib.net/

	std::vector <unsigned char>Buffer_Vec;

	constexpr size_t BUFSIZE = 524288;

	unsigned char* temp_buffer{ new unsigned char[BUFSIZE] };

	z_stream strm;
	strm.zalloc = 0;
	strm.zfree = 0;
	strm.next_in = Vec.data();
	strm.avail_in = Vec.size();
	strm.next_out = temp_buffer;
	strm.avail_out = BUFSIZE; // Buffer size.

	if (embed_file_mode) {
		deflateInit(&strm, 6); // Compression level 6 (78, 9C...)
	}
	else {
		inflateInit(&strm);
	}

	while (strm.avail_in)
	{
		if (embed_file_mode) {
			deflate(&strm, Z_NO_FLUSH);
		}
		else {
			inflate(&strm, Z_NO_FLUSH);
		}

		if (!strm.avail_out)
		{
			Buffer_Vec.insert(Buffer_Vec.end(), temp_buffer, temp_buffer + BUFSIZE);
			strm.next_out = temp_buffer;
			strm.avail_out = BUFSIZE;
		}
		else
			break;
	}
	if (embed_file_mode) {
		deflate(&strm, Z_FINISH);
		Buffer_Vec.insert(Buffer_Vec.end(), temp_buffer, temp_buffer + BUFSIZE - strm.avail_out);
		deflateEnd(&strm);
	}
	else {
		inflate(&strm, Z_FINISH);
		Buffer_Vec.insert(Buffer_Vec.end(), temp_buffer, temp_buffer + BUFSIZE - strm.avail_out);
		inflateEnd(&strm);
	}
	Vec.swap(Buffer_Vec);
	delete[] temp_buffer;
}

void Erase_Chunks(PDV_STRUCT& pdv) {

	// Keep the critical PNG chunks: IHDR, *PLTE, IDAT & IEND.

	std::vector<unsigned char>Temp_Vec;

	// Copy the first 33 bytes of Image_Vec into Temp_Vec (PNG header + IHDR).
	Temp_Vec.insert(Temp_Vec.begin(), pdv.Image_Vec.begin(), pdv.Image_Vec.begin() + 33);

	const std::string IDAT_SIG = "IDAT";

	// Get first IDAT chunk index.
	size_t idat_index = std::search(pdv.Image_Vec.begin(), pdv.Image_Vec.end(), IDAT_SIG.begin(), IDAT_SIG.end()) - pdv.Image_Vec.begin() - 4;  // -4 is to position the index at the start of the IDAT chunk's length field.

	// Make sure this is a valid IDAT chunk. Check CRC value.

	// Get first IDAT chunk length value
	const size_t FIRST_IDAT_LENGTH = ((static_cast<size_t>(pdv.Image_Vec[idat_index]) << 24)
		| (static_cast<size_t>(pdv.Image_Vec[idat_index + 1]) << 16)
		| (static_cast<size_t>(pdv.Image_Vec[idat_index + 2]) << 8)
		| (static_cast<size_t>(pdv.Image_Vec[idat_index + 3])));

	// Get first IDAT chunk's CRC index
	size_t first_idat_crc_index = idat_index + FIRST_IDAT_LENGTH + 8;

	const size_t 
		FIRST_IDAT_CRC = ((static_cast<size_t>(pdv.Image_Vec[first_idat_crc_index]) << 24)
		| (static_cast<size_t>(pdv.Image_Vec[first_idat_crc_index + 1]) << 16)
		| (static_cast<size_t>(pdv.Image_Vec[first_idat_crc_index + 2]) << 8)
		| (static_cast<size_t>(pdv.Image_Vec[first_idat_crc_index + 3]))),
		
		CALC_FIRST_IDAT_CRC = Crc(&pdv.Image_Vec[idat_index + 4], FIRST_IDAT_LENGTH + 4);

	// Make sure values match.
	if (FIRST_IDAT_CRC != CALC_FIRST_IDAT_CRC) {
		std::cerr << "\nImage File Error: CRC value for first IDAT chunk is invalid.\n\n";
		std::exit(EXIT_FAILURE);
	}

	// *For PNG-8 Indexed color (3) we need to keep the PLTE chunk.
	if (Temp_Vec[25] == 3) {

		const std::string PLTE_SIG = "PLTE";

		// Find PLTE chunk index and copy its contents into Temp_Vec.
		const size_t PLTE_CHUNK_INDEX = std::search(pdv.Image_Vec.begin(), pdv.Image_Vec.end(), PLTE_SIG.begin(), PLTE_SIG.end()) - pdv.Image_Vec.begin() - 4;

		if (idat_index > PLTE_CHUNK_INDEX) {
			const size_t CHUNK_SIZE = ((static_cast<size_t>(pdv.Image_Vec[PLTE_CHUNK_INDEX + 1]) << 16)
				| (static_cast<size_t>(pdv.Image_Vec[PLTE_CHUNK_INDEX + 2]) << 8)
				| (static_cast<size_t>(pdv.Image_Vec[PLTE_CHUNK_INDEX + 3])));

			Temp_Vec.insert(Temp_Vec.end(), pdv.Image_Vec.begin() + PLTE_CHUNK_INDEX, pdv.Image_Vec.begin() + PLTE_CHUNK_INDEX + (CHUNK_SIZE + 12));
		}
		else {
			std::cerr << "\nImage File Error: Required PLTE chunk not found for Indexed-color (PNG-8) image.\n\n";
			std::exit(EXIT_FAILURE);
		}
	}

	// Find all the IDAT chunks and copy them into Temp_Vec.
	while (pdv.image_size != idat_index + 4) {
		const size_t CHUNK_SIZE = ((static_cast<size_t>(pdv.Image_Vec[idat_index]) << 24)
			| (static_cast<size_t>(pdv.Image_Vec[idat_index + 1]) << 16)
			| (static_cast<size_t>(pdv.Image_Vec[idat_index + 2]) << 8)
			| (static_cast<size_t>(pdv.Image_Vec[idat_index + 3])));

		Temp_Vec.insert(Temp_Vec.end(), pdv.Image_Vec.begin() + idat_index, pdv.Image_Vec.begin() + idat_index + (CHUNK_SIZE + 12));
		idat_index = std::search(pdv.Image_Vec.begin() + idat_index + 6, pdv.Image_Vec.end(), IDAT_SIG.begin(), IDAT_SIG.end()) - pdv.Image_Vec.begin() - 4;
	}

	// Copy the last 12 bytes of Image_Vec into Temp_Vec.
	Temp_Vec.insert(Temp_Vec.end(), pdv.Image_Vec.end() - 12, pdv.Image_Vec.end());

	Temp_Vec.swap(pdv.Image_Vec);

	// Update image size variable.
	pdv.image_size = pdv.Image_Vec.size();

	Check_Data_File(pdv);
}

uint_fast64_t Crc_Update(const uint_fast64_t& Crc, unsigned char* buf, const uint_fast64_t& len)
{
	// Table of CRCs of all 8-bit messages.
	constexpr size_t Crc_Table[256]{
		0x00, 	    0x77073096, 0xEE0E612C, 0x990951BA, 0x76DC419,  0x706AF48F, 0xE963A535, 0x9E6495A3, 0xEDB8832,  0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x9B64C2B,  0x7EB17CBD,
		0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7, 0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
		0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
		0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59, 0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
		0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x1DB7106,  0x98D220BC, 0xEFD5102A, 0x71B18589, 0x6B6B51F,
		0x9FBFE4A5, 0xE8B8D433, 0x7807C9A2, 0xF00F934,  0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x86D3D2D,	0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
		0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65, 0x4DB26158, 0x3AB551CE,
		0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
		0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F, 0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
		0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x3B6E20C,  0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x4DB2615,  0x73DC1683, 0xE3630B12, 0x94643B84, 0xD6D6A3E,  0x7A6A5AA8,
		0xE40ECF0B, 0x9309FF9D, 0xA00AE27,  0x7D079EB1, 0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0,
		0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
		0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703,
		0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D, 0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x26D930A,
		0x9C0906A9, 0xEB0E363F, 0x72076785, 0x5005713,  0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0xCB61B38,  0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0xBDBDF21,  0x86D3D2D4, 0xF1D4E242,
		0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777, 0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
		0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5,
		0x47B2CF7F, 0x30B5FFE9, 0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
		0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D };

	// Update a running CRC with the bytes buf[0..len - 1] the CRC should be initialized to all 1's, 
	// and the transmitted value is the 1's complement of the final running CRC (see the Crc() routine below).
	uint_fast64_t c = Crc;
	uint_fast32_t n;

	for (n = 0; n < len; n++) {
		c = Crc_Table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
	}

	return c;
}

// Return the CRC of the bytes buf[0..len-1].
uint_fast64_t Crc(unsigned char* buf, const uint_fast64_t& len)
{
	return Crc_Update(0xffffffffL, buf, len) ^ 0xffffffffL;
}

void Write_Out_File(PDV_STRUCT& pdv) {

	std::ofstream write_file_fs(pdv.data_file_name, std::ios::binary);

	if (!write_file_fs) {
		std::cerr << "\nWrite File Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}

	if (pdv.embed_file_mode) {

		pdv.image_size = pdv.Image_Vec.size();

		// Write out to disk the PNG image embedded with the user's encrypted/compressed data file.
		write_file_fs.write((char*)&pdv.Image_Vec[0], pdv.Image_Vec.size());

		if (pdv.mastodon_opt || pdv.reddit_opt) {
			std::string option = pdv.mastodon_opt ? "Mastodon" : "Reddit";
			std::cout << "\n**Warning**\n\nDue to your option selection, for compatibility reasons\nyou should only post this file-embedded PNG image on " + option + ".\n";
		}
		std::cout << "\nSaved PNG image: " + pdv.data_file_name + '\x20' + std::to_string(pdv.image_size) + " Bytes.\n\nComplete!\n\nYou can now post your file-embedded PNG image on the relevant supported platforms.\n\n";
	}
	else {
		pdv.data_size = pdv.File_Vec.size();

		std::cout << "\nWriting data file out to disk.\n";

		// Write out to disk the extracted data file.
		write_file_fs.write((char*)&pdv.File_Vec[0], pdv.data_size);

		std::cout << "\nSaved data file: " + pdv.data_file_name + '\x20' + std::to_string(pdv.data_size) + " Bytes.\n\nComplete! Please check your extracted file.\n\n";
	}
}

// Update values, such as chunk lengths, file sizes, CRC, etc. Write them into the relevant vector index locations. Overwrites previous values.
void Value_Updater(std::vector<unsigned char>& vec, size_t value_insert_index, const size_t& VALUE, uint_fast8_t bits) {
	while (bits) {
		static_cast<size_t>(vec[value_insert_index++] = (VALUE >> (bits -= 8)) & 0xff);
	}
}

void Check_Arguments_Input(const std::string& FILE_NAME_INPUT) {

	const std::regex REG_EXP("(\\.[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+)?[a-zA-Z_0-9\\.\\\\\\s\\-\\/]+?(\\.[a-zA-Z0-9]+)?");

	if (!regex_match(FILE_NAME_INPUT, REG_EXP)) {
		std::cerr << "\nInvalid Input Error: Your file name: \"" + FILE_NAME_INPUT + "\" contains characters not supported by this program.\n\n";
		std::exit(EXIT_FAILURE);
	}
}

void Display_Info() {

	std::cout << R"(
PNG Data Vehicle (pdvrdt v1.8). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

A simple command-line tool used to embed and extract any file type via a PNG image file.  
Share your file-embedded image on the following compatible sites.  

Image size limit is platform dependant:-  

  Flickr (200MB), ImgBB (32MB), PostImage (24MB), *Reddit (19MB / -r option), 
  *Mastodon (16MB / -m option), *ImgPile (8MB), *Twitter (5MB / Dimension limits).

Argument options:	

 -e = Embed File Mode, 
 -x = eXtract File Mode,
 -m = Mastodon option, used with -e Embed mode (pdvrdt -e -m image.png file.mp3).
 -r = Reddit option, used with -e Embed mode (pdvrdt -e -r image.png file.doc).

To post/share file-embedded PNG images on Reddit, you need to use the -r option.
To post/share file-embedded PNG images on Mastodon, you need to use the -m opion.
	
*Reddit - Post images via the new.reddit.com desktop / browser site only. Use the "Images & Video" tab/box.

*Twitter - As well as the 5MB PNG image size limit, Twitter also has dimension size limits.
PNG-32/24 (Truecolor) 900x900 Max. 68x68 Min. 
PNG-8 (Indexed color) 4096x4096 Max. 68x68 Min.

*ImgPile - You must sign in to an account before sharing your file-embedded PNG image on this platform.
Sharing your image without logging in, your embedded file will not be preserved.

This program works on Linux and Windows.

)";
}
