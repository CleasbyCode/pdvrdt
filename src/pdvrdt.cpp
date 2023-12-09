//	PNG Data Vehicle (pdvrdt v1.6). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023
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

typedef unsigned char BYTE;

struct PDV_STRUCT {
	const size_t MAX_FILE_SIZE = 209715200, MAX_FILE_SIZE_REDDIT = 20971520, PNG_MIN_SIZE = 68;
	std::vector<BYTE> Image_Vec, File_Vec, Profile_Data_Vec, Profile_Chunk_Vec, Decrypted_Vec;
	std::string image_file_name, data_file_name;
	size_t image_size{}, data_size{};
	uint_fast8_t file_count{}, sub_file_count{};
	bool insert_file_mode = false, extract_file_mode = false, reddit_opt = false, inflate_data = false, deflate_data = false;
};
	
void
	// Attempt to open image file, followed by some basic checks to make sure user's image file meets program requirements.
	Check_Image_File(PDV_STRUCT&),

	// Attempt to open data file, followed by some basic checks to make sure user's data file meets program requirements.
	Check_Data_File(PDV_STRUCT&),

	// Fill vector with our ICC Profile data.
	Fill_Profile_Vec(PDV_STRUCT&),

	// Start the data file extraction process.
	Extract_Data_File(PDV_STRUCT&),

	// Encrypt or decrypt user's data file, depending on mode.
	Encrypt_Decrypt(PDV_STRUCT& pdv),

	// Search and remove all unnecessary PNG chunks found before the first IDAT chunk.
	Erase_Chunks(PDV_STRUCT& pdv),

	// Inflate or Deflate iCCP Profile chunk, which include user's encrypted data file, depending on mode.
	Inflate_Deflate(std::vector<BYTE>&, bool),

	// Write out to file the file-embedded image or the extracted data file.
	Write_Out_File(PDV_STRUCT& pdv),

	// Update values, such as chunk lengths, CRC, file sizes, etc. Writes them into the relevant vector index locations.
	Value_Updater(std::vector<BYTE>&, size_t, const size_t&, uint_fast8_t),

	// Check args input for invalid data.
	Check_Arguments_Input(const std::string&),

	// Display program information.
	Display_Info();

// Code to compute CRC32 (for iCCP Profile chunk within this program).  https://www.w3.org/TR/2003/REC-PNG-20031110/#D-CRCAppendix 
uint_fast64_t
	Crc_Update(const uint_fast64_t&, BYTE*, const uint_fast64_t&),
	Crc(BYTE*, const uint_fast64_t&);

int main(int argc, char** argv) {

	PDV_STRUCT pdv;

	if (argc == 2 && std::string(argv[1]) == "--info") {
		argc = 0;
		Display_Info();
	} else if (argc >= 4 && argc < 10 && std::string(argv[1]) == "-i") {

		// Insert file mode.

		if (std::string(argv[2]) == "-r") {
			pdv.reddit_opt = true;
		}

		pdv.insert_file_mode = true, pdv.deflate_data = true;
		pdv.sub_file_count = pdv.reddit_opt ? argc - 2 : argc - 1;
		pdv.image_file_name = pdv.reddit_opt ? argv[3] : argv[2];
		pdv.reddit_opt ? argc -= 3 : argc -= 2;

		Check_Arguments_Input(pdv.image_file_name);

		while (argc != 1) {
			pdv.file_count = argc;
			pdv.data_file_name = pdv.reddit_opt ? argv[4] : argv[3];
			Check_Arguments_Input(pdv.data_file_name);
			Check_Image_File(pdv);
			argv++, argc--;	// Move to next data file, reduce file count.
		}

		argc = 1;

	} else if (argc >= 3 && argc < 9 && std::string(argv[1]) == "-x") {

		// Extract file mode.

		pdv.extract_file_mode = true;

		while (argc >= 3) {
			pdv.image_file_name = argv[2];
			Check_Arguments_Input(pdv.image_file_name);
			Check_Image_File(pdv);
			argv++, argc--;	// Move to next embedded image file, reduce file count.
		}

	} else {
		std::cerr << "\nUsage:\tpdvrdt -i [-r] <cover_image> <file(s)>\n\tpdvrdt -x <embedded_image(s)>\n\tpdvrdt --info\n\n";
		argc = 0;
	}
	if (argc != 0) {
		if (argc == 2) {
			std::cout << "\nComplete! Please check your extracted file(s).\n\n";
		} else {
			std::cout << "\nComplete!\n\nYou can now post your file-embedded PNG image(s) on the relevant supported platforms.\n\n";
		}
	}
	return 0;
}

void Check_Image_File(PDV_STRUCT& pdv) {

	const std::string GET_IMAGE_EXTENSION = pdv.image_file_name.length() > 2 ? pdv.image_file_name.substr(pdv.image_file_name.length() - 3) : pdv.image_file_name;

	std::ifstream read_image_fs(pdv.image_file_name, std::ios::binary);

	if (!read_image_fs || GET_IMAGE_EXTENSION !="png") {
		std::cerr << (!read_image_fs ? "\nRead File Error: Unable to open image file" 
				: "\nImage File Error: Invalid file extension. Only expecting 'png' for image file") << ".\n\n";
		std::exit(EXIT_FAILURE);
	}
	// Check PNG image for valid file size requirements.
	pdv.image_size = std::filesystem::file_size(pdv.image_file_name);

	if (pdv.image_size > pdv.MAX_FILE_SIZE || pdv.reddit_opt && pdv.image_size > pdv.MAX_FILE_SIZE_REDDIT || pdv.PNG_MIN_SIZE > pdv.image_size) {
		// Image size is too small or larger than the set size limit. Display relevant error message and exit program.
		std::cerr << "\nImage File Error: "  << (pdv.PNG_MIN_SIZE > pdv.image_size ? "Size of image is too small to be a valid PNG image" 
			: "Size of image exceeds the maximum limit of " + (pdv.reddit_opt ? std::to_string(pdv.MAX_FILE_SIZE_REDDIT) 
			: std::to_string(pdv.MAX_FILE_SIZE)) + " Bytes") << ".\n\n";
		std::exit(EXIT_FAILURE);
	}
	
	// Display Start message. Different depending on mode and options selected.
	std::cout << (pdv.insert_file_mode && pdv.reddit_opt ? "\nInsert mode selected with -r option.\n\nReading files"
				: (pdv.insert_file_mode ? "\nInsert mode selected.\n\nReading files"
				: "\nExtract mode selected.\n\nReading embedded PNG image file")) << ". Please wait...\n";

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

	if (pdv.extract_file_mode) {
		Extract_Data_File(pdv);
	} else {
		// Find and remove unwanted PNG chunks.
		Erase_Chunks(pdv);
	}
}

void Check_Data_File(PDV_STRUCT& pdv) {

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
	const uint_fast8_t MAX_FILENAME_LENGTH = 23;

	if (pdv.data_size > pdv.MAX_FILE_SIZE || pdv.reddit_opt && pdv.data_size > pdv.MAX_FILE_SIZE_REDDIT || FILE_NAME_LENGTH > pdv.data_size || FILE_NAME_LENGTH > MAX_FILENAME_LENGTH) {
		// File name too long, or image size is too small or larger than size limit. Display relevant error message and exit program.
		std::cerr << "\nData File Error: " << (FILE_NAME_LENGTH > MAX_FILENAME_LENGTH ? "Length of file name is too long.\n\nFor compatibility requirements, length of file name must be under 24 characters"
		: (FILE_NAME_LENGTH > pdv.data_size ? "Size of file is too small.\n\nFor compatibility requirements, file size must be greater than the length of the file name"
		: "Size of file exceeds the maximum limit of " + (pdv.reddit_opt ? std::to_string(pdv.MAX_FILE_SIZE_REDDIT) : std::to_string(pdv.MAX_FILE_SIZE)) + " Bytes")) << ".\n\n";
		std::exit(EXIT_FAILURE);
	}

	// Read-in user's data file and store it in vector "File_Vec".
	pdv.File_Vec.assign(std::istreambuf_iterator<char>(read_file_fs), std::istreambuf_iterator<char>());

	// Update size variable of the user's data file stored in vector.
	pdv.data_size = pdv.File_Vec.size();

	// Combined file size check.
	if (pdv.image_size + pdv.data_size > pdv.MAX_FILE_SIZE || pdv.reddit_opt && pdv.image_size + pdv.data_size > pdv.MAX_FILE_SIZE_REDDIT ) {
		// File size check failure, display relevant error message and exit program.

		std::cerr << "\nFile Size Error: The combined file size of the PNG image & data file, must not exceed " << (pdv.reddit_opt ? std::to_string(pdv.MAX_FILE_SIZE_REDDIT) : std::to_string(pdv.MAX_FILE_SIZE)) << " Bytes.\n\n";
		std::exit(EXIT_FAILURE);
	}

	Fill_Profile_Vec(pdv);
}

void Fill_Profile_Vec(PDV_STRUCT& pdv) {

	// The first 401 bytes of the vector "Profile_Data_Vec" contains the basic profile, followed by a 3-byte signature "PDV". 404 bytes total.
	// The length value of the user's data file name and the encrypted file name are stored within this profile.
	// After the user's data file has been encrypted and compressed, it is inserted and stored at the end of this profile.

	pdv.Profile_Data_Vec = {
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
	},
		
	// The above vector "Profile_Data_Vec", containing the basic profile, that will later be compressed (deflate), 
	// and will also contain the encrypted & compressed user's data file, will be inserted into this vector "Profile_Chunk_Vec". 
	// The vector initially contains just the PNG iCCP chunk header (Length, Name and CRC value fields).
		
	pdv.Profile_Chunk_Vec = {
		0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	std::cout << "\nEncrypting data file.\n";

	// Encrypt the user's data file and its file name.
	Encrypt_Decrypt(pdv);
}

void Extract_Data_File(PDV_STRUCT& pdv) {
	
	// Extract mode. Start extraction process.
	
	// Check for and remove unwanted chunks (again). 
	// Some media sites may add various PNG chunks to the downloaded image, which could cause issues with the expected location of the iCCP chunk.
	Erase_Chunks(pdv);
	
	pdv.data_size = pdv.Image_Vec.size();

	uint_fast8_t profile_chunk_index = 37;	// iCCP chunk location within the PNG image file. 

	// iCCP Profile chunk signature.
	const std::string PROFILE_SIG = "iCCPicc";
		
	// Make sure iCCP chunk exists within the PNG image and at the correct index location.
	const auto PROFILE_POS = std::search(pdv.Image_Vec.begin(), pdv.Image_Vec.end(), PROFILE_SIG.begin(), PROFILE_SIG.end()) - pdv.Image_Vec.begin();

	if (PROFILE_POS > profile_chunk_index) { // Either reached end of image file without finding a iCCP chunk, or found the chunk, but not in the required location.

		// File requirement check failure, display relevant error message and exit program.
		std::cerr << "\nImage File Error: This is not a pdvrdt file-embedded image.\n\n";
		std::exit(EXIT_FAILURE);
	}

	std::cout << "\nFound compressed iCCP chunk.\n";

	const uint_fast8_t
		PROFILE_CHUNK_SIZE_INDEX = 33,	// Start index location of 4 byte iCCP Profile chunk length field.
		DEFLATE_DATA_INDEX = 46;	// Start index location of deflate data within iCCP Profile chunk. (78, 9C...).

	pdv.Image_Vec[DEFLATE_DATA_INDEX] = '\x78'; // Put back 0x78 char, just in case is was removed by the Reddit option (-r). 

	const size_t
		// Get iCCP Profile chunk length.
		PROFILE_CHUNK_SIZE = ((static_cast<size_t>(pdv.Image_Vec[PROFILE_CHUNK_SIZE_INDEX]) << 24) 
			| (static_cast<size_t>(pdv.Image_Vec[PROFILE_CHUNK_SIZE_INDEX + 1]) << 16) 
			| (static_cast<size_t>(pdv.Image_Vec[PROFILE_CHUNK_SIZE_INDEX + 2]) << 8)
			| (static_cast<size_t>(pdv.Image_Vec[PROFILE_CHUNK_SIZE_INDEX + 3]))),

		DEFLATE_CHUNK_SIZE = PROFILE_CHUNK_SIZE - PROFILE_SIG.length() + 2; // + 2 includes the two zero bytes after the iCCPicc chunk name.

	// From "Image_Vec" vector index 0, erase n bytes so that the start of the vector's contents is now the beginning of the deflate data (78,9C...).
	pdv.Image_Vec.erase(pdv.Image_Vec.begin(), pdv.Image_Vec.begin() + DEFLATE_DATA_INDEX);

	// Erase all bytes of "Image_Vec" after the end of the deflate data. Vector should now just contain the deflate data.
	pdv.Image_Vec.erase(pdv.Image_Vec.begin() + DEFLATE_CHUNK_SIZE, pdv.Image_Vec.end());

	std::cout << "\nInflating iCCP chunk.\n";

	// Call function to inflate the profile chunk, which includes user's encrypted data file. 
	Inflate_Deflate(pdv.Image_Vec, pdv.inflate_data);

	const uint_fast8_t
		FILENAME_LENGTH = pdv.Image_Vec[100],	// From vector index location, get stored length value of the embedded file name of user's data file.
		FILENAME_START_INDEX = 101;		// Vector index start location of the embedded file name of user's data file.

	const uint_fast16_t
		PDV_SIG_START_INDEX = 401,		// Vector index for this program's embedded signature.
		DATA_FILE_START_INDEX = 404;		// Vector index start location for user's embedded data file.

	// Get encrypted embedded file name from vector "Image_Vec".
	pdv.data_file_name = { pdv.Image_Vec.begin() + FILENAME_START_INDEX, pdv.Image_Vec.begin() + FILENAME_START_INDEX + FILENAME_LENGTH };

	const std::string
		PDV_SIG = "PDV",	// pdvrdt signature.
		// Attempt to get the pdvrdt signature from vector "Image_Vec".
		GET_PDV_SIG{ pdv.Image_Vec.begin() + PDV_SIG_START_INDEX, pdv.Image_Vec.begin() + PDV_SIG_START_INDEX + PDV_SIG.length() };

	// Make sure this is a pdvrdt file-embedded image.
	if (GET_PDV_SIG != PDV_SIG) {
		std::cerr << "\nImage File Error: Profile does not appear to be a pdvrdt file-embedded profile.\n\n";
		std::exit(EXIT_FAILURE);
	}

	std::cout << "\nFound pdvrdt file-embedded image.\n\nExtracting encrypted file from the iCCP chunk.\n";

	// Delete the profile from the inflated file, leaving just our encrypted data file.
	pdv.Image_Vec.erase(pdv.Image_Vec.begin(), pdv.Image_Vec.begin() + DATA_FILE_START_INDEX);

	std::cout << "\nDecrypting extracted file.\n";

	// Decrypt the contents of vector "Image_Vec".
	Encrypt_Decrypt(pdv);
}

void Encrypt_Decrypt(PDV_STRUCT& pdv) {

	const std::string
		XOR_KEY = "\xFC\xD8\xF9\xE2\x9F\x7E",	// String used to XOR encrypt/decrypt the file name of user's data file.
		INPUT_NAME = pdv.data_file_name;

	std::string output_name;

	size_t
		file_size = pdv.insert_file_mode ? pdv.File_Vec.size() : pdv.Image_Vec.size(),	 // File size of user's data file.
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

		if (pdv.insert_file_mode) {
			// Encrypt data file. XOR each byte of the data file within vector "File_Vec" against each character of the encrypted file name, "output_name". 
			// Store encrypted output in vector "Profile_Data_Vec".
			pdv.Profile_Data_Vec.emplace_back(pdv.File_Vec[index_pos++] ^ output_name[name_key_pos++]);
		}
		else {
			// Decrypt data file: XOR each byte of the data file within vector "Image_Vec" against each character of the encrypted file name, "INPUT_NAME". 
			// Store decrypted output in vector "Decrypted_Vec".
			pdv.Decrypted_Vec.emplace_back(pdv.Image_Vec[index_pos++] ^ INPUT_NAME[name_key_pos++]);
		}
	}

	if (pdv.insert_file_mode) {

		const uint_fast8_t
			PROFILE_NAME_LENGTH_INDEX = 100,	// Index location inside the compressed profile to store the length value of the user's data file name (1 byte).
			PROFILE_NAME_INDEX = 101;		// Start index location inside the compressed profile to store the encrypted file name for the user's embedded file.

		uint_fast8_t
			profile_data_vec_size_index = 0,	// Start index location of the compressed profile's 4 byte length field.
			profile_chunk_size_index = 0,		// Start index of the PNG iCCP chunk's 4 byte length field.
			bits = 32;				// Value used in the "Value_Update" function. 4 bytes.

		// Insert the character length value of the file name into the profile.
		// We need to know how many characters to read when we later retrieve the file name from the profile, during file extraction.
		pdv.Profile_Data_Vec[PROFILE_NAME_LENGTH_INDEX] = static_cast<BYTE>(INPUT_NAME.length());

		// Insert the encrypted filename into the profile.
		std::copy(output_name.begin(), output_name.end(), pdv.Profile_Data_Vec.begin() + PROFILE_NAME_INDEX);

		// Update internal profile size.
		Value_Updater(pdv.Profile_Data_Vec, profile_data_vec_size_index, pdv.Profile_Data_Vec.size(), bits);

		std::cout << "\nCompressing data file.\n";

		// Call function to deflate the contents of vector "Profile_Data_Vec" (profile with user's encrypted file).
		Inflate_Deflate(pdv.Profile_Data_Vec, pdv.deflate_data);

		if (pdv.reddit_opt) {
			pdv.Profile_Data_Vec[0] = '\x00';
		}

		const size_t
			PROFILE_DEFLATE_SIZE = pdv.Profile_Data_Vec.size(),
			PROFILE_CHUNK_SIZE = PROFILE_DEFLATE_SIZE + 9; // "iCCPicc\x00\x00" (+ 9 bytes).

		const uint_fast8_t
			PROFILE_DATA_INSERT_INDEX = 13,   // Index location within Profile_Chunk_Vec to insert the deflate contents of Profile_Data_Vec. 		   
			PROFILE_CHUNK_INSERT_INDEX = 33,  // Index location within Image_Vec (image file) to insert Profile_Chunk_Vec (iCCP chunk).
			PROFILE_CHUNK_START_INDEX = 4;

		// Insert the deflate/compressed iCCP chunk (with user's encrypted/compressed file) into vector "Profile_Chunk_Vec".
		pdv.Profile_Chunk_Vec.insert((pdv.Profile_Chunk_Vec.begin() + PROFILE_DATA_INSERT_INDEX), pdv.Profile_Data_Vec.begin(), pdv.Profile_Data_Vec.end());

		// Call function to insert new chunk length value into "Profile_Chunk_Vec" vector's index length field. 
		Value_Updater(pdv.Profile_Chunk_Vec, profile_chunk_size_index, PROFILE_DEFLATE_SIZE + 5, bits);

		// Pass these two values (PROFILE_CHUNK_START_INDEX and PROFILE_CHUNK_SIZE) to the CRC fuction to get correct iCCP chunk CRC.
		const size_t PROFILE_CHUNK_CRC = Crc(&pdv.Profile_Chunk_Vec[PROFILE_CHUNK_START_INDEX], PROFILE_CHUNK_SIZE);

		// Index location for the 4-byte CRC field of the iCCP chunk.
		size_t profile_crc_insert_index = PROFILE_CHUNK_START_INDEX + PROFILE_CHUNK_SIZE;

		// Call function to insert iCCP chunk CRC value into "Profile_Chunk_Vec" vector's CRC index field. 
		Value_Updater(pdv.Profile_Chunk_Vec, profile_crc_insert_index, PROFILE_CHUNK_CRC, bits);

		std::cout << "\nEmbedding data file within the iCCP chunk of the PNG image.\n";

		// Insert contents of vector "Profile_Chunk_Vec" into vector "Image_Vec", combining iCCP chunk (compressed profile & user's data file) with PNG image.
		pdv.Image_Vec.insert((pdv.Image_Vec.begin() + PROFILE_CHUNK_INSERT_INDEX), pdv.Profile_Chunk_Vec.begin(), pdv.Profile_Chunk_Vec.end());

		// If we embed multiple data files (Max. 6), each outputted image will be differentiated by a number in the file name, e.g. pdv_img1.jpg, pdv_img2.jpg, pdv_img3.jpg.
		const std::string DIFF_VALUE = std::to_string(pdv.sub_file_count - pdv.file_count);
		pdv.data_file_name = "pdv_img" + DIFF_VALUE + ".png";

		std::cout << "\nWriting file-embedded PNG image out to disk.\n";

		// Write out to file the PNG image with the embedded (encrypted/compressed) user's data file.
		Write_Out_File(pdv);
	}
	else { // Extract Mode.
	
		// Update string variable with the decrypted file name.
		pdv.data_file_name = output_name;

		// Write the extracted (inflated / decrypted) data file out to disk.
		Write_Out_File(pdv);
	}
}

void Inflate_Deflate(std::vector<BYTE>& Vec, bool deflate_data) {

	// zlib function, see https://zlib.net/

	std::vector <BYTE> Buffer_Vec;

	const size_t
		VEC_SIZE = Vec.size(),
		BUFSIZE = 512 * 1024;

	BYTE* temp_buffer{ new BYTE[BUFSIZE] };

	z_stream strm;
	strm.zalloc = 0;
	strm.zfree = 0;
	strm.next_in = Vec.data();
	strm.avail_in = VEC_SIZE;
	strm.next_out = temp_buffer;
	strm.avail_out = BUFSIZE;

	if (deflate_data) {
		deflateInit(&strm, 6); // Compression level 6 (78, 9C...)
	}
	else {
		inflateInit(&strm);
	}

	while (strm.avail_in)
	{
		if (deflate_data) {
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
	if (deflate_data) {
		deflate(&strm, Z_FINISH);
		Buffer_Vec.insert(Buffer_Vec.end(), temp_buffer, temp_buffer + BUFSIZE - strm.avail_out);
		deflateEnd(&strm);
	}
	else {
		inflate(&strm, Z_FINISH);
		Buffer_Vec.insert(Buffer_Vec.end(), temp_buffer, temp_buffer + BUFSIZE - strm.avail_out);
		inflateEnd(&strm);
	}

	Vec.clear();
	Vec.swap(Buffer_Vec);
	delete[] temp_buffer;
	Buffer_Vec.clear();
}

void Erase_Chunks(PDV_STRUCT& pdv) {

	std::string chunks[15]{ "IDAT", "bKGD", "cHRM", "sRGB", "hIST", "iCCP", "pHYs", "sBIT", "gAMA", "sPLT", "tIME", "tRNS", "tEXt", "iTXt", "zTXt" };

	if (pdv.extract_file_mode) {  
		// If extracting data, we need to keep the iCCP chunk, so rename it to prevent removal. 
		// Also change the first (0) name element to "iCCP", so that it becomes our search limit marker. See for-loop below.
		chunks[5] = "pdVRdt_KEeP_Me_PDvrDT";
		chunks[0] = "iCCP";
	}

	for (uint_fast8_t chunk_index = 14; chunk_index > 0; --chunk_index) {
		// If insert mode, get first IDAT chunk index location. Don't remove chunks after this search limit location.
		// If extract mode, get iCCP chunk index location. Don't remove chunks after this search limit location.
		const size_t
			SEARCH_LIMIT_INDEX = std::search(pdv.Image_Vec.begin(), pdv.Image_Vec.end(), chunks[0].begin(), chunks[0].end()) - pdv.Image_Vec.begin(),
			// From last to first, search and get index location of each chunk to remove.
			CHUNK_FOUND_INDEX = std::search(pdv.Image_Vec.begin(), pdv.Image_Vec.end(), chunks[chunk_index].begin(), chunks[chunk_index].end()) - pdv.Image_Vec.begin() - 4;

		// If the found chunk is located before search limit location, we can remove it.
		if (SEARCH_LIMIT_INDEX > CHUNK_FOUND_INDEX && SEARCH_LIMIT_INDEX != pdv.Image_Vec.size()) {
			
			// Get the length of the chunk.
			const size_t CHUNK_SIZE = ((static_cast<size_t>(pdv.Image_Vec[CHUNK_FOUND_INDEX]) << 24) 
					| (static_cast<size_t>(pdv.Image_Vec[CHUNK_FOUND_INDEX + 1]) << 16) 
					| (static_cast<size_t>(pdv.Image_Vec[CHUNK_FOUND_INDEX + 2]) << 8)
					| (static_cast<size_t>(pdv.Image_Vec[CHUNK_FOUND_INDEX + 3])));

			pdv.Image_Vec.erase(pdv.Image_Vec.begin() + CHUNK_FOUND_INDEX, pdv.Image_Vec.begin() + CHUNK_FOUND_INDEX + (CHUNK_SIZE + 12));
			++chunk_index; // Increment chunk name element value so that we search again for the same chunk, in case of multiple occurrences.
		}
	}

	if (pdv.insert_file_mode) {

		// Update image size variable in case chunks deleted.
		pdv.image_size = pdv.Image_Vec.size();

		Check_Data_File(pdv);
	}
}

uint_fast64_t Crc_Update(const uint_fast64_t& Crc, BYTE* buf, const uint_fast64_t& len)
{
	// Table of CRCs of all 8-bit messages.
	const size_t Crc_Table[256]{
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
uint_fast64_t Crc(BYTE* buf, const uint_fast64_t& len)
{
	return Crc_Update(0xffffffffL, buf, len) ^ 0xffffffffL;
}

void Write_Out_File(PDV_STRUCT& pdv) {

	std::ofstream write_file_fs(pdv.data_file_name, std::ios::binary);

	if (!write_file_fs) {
		std::cerr << "\nWrite File Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}

	if (pdv.insert_file_mode) {

		// Write out to disk the PNG image embedded with the user's encrypted/compressed data file.
		write_file_fs.write((char*)&pdv.Image_Vec[0], pdv.Image_Vec.size());

		std::string size_warning =
			"\n**Warning**\n\nDue to the file size of your file-embedded PNG image,\nyou will only be able to share this image on the following platforms:\n\n"
			"Flickr, ImgBB, PostImage, Mastodon, ImgPile & Imgur";

		const size_t 
			MSG_LEN = size_warning.length(),
			IMG_SIZE = pdv.Image_Vec.size();

		const uint_fast32_t
			// Twitter 9.5KB. While not officially supported because of the tiny size requirement, if your data file is this size 
			// (9.5KB, 9836 bytes) or lower with image dimensions 900x900 or less (PNG-32/24) 4096x4096 or less (PNG-8), 
			// then you should be able to use Twitter to share your file-embedded image.
			REDDIT_SIZE = 1048172,		// Just under 1MB. (Default size without the -r option).
			IMGUR_SIZE = 5242880,		// 5MB
			IMG_PILE_SIZE = 8388608,	// 8MB
			MASTODON_SIZE = 16777216,	// 16MB
			REDDIT_OPTION_SIZE = 20971520,  // 20MB (Only with the -r option selected).
			POST_IMG_SIZE = 25165824,	// 24MB
			IMGBB_SIZE = 33554432;		// 32MB
			// Flickr is 200MB, max size for this program, no need to make a variable for it.

		size_warning = (IMG_SIZE > IMGUR_SIZE && IMG_SIZE <= IMG_PILE_SIZE ? size_warning.substr(0, MSG_LEN - 8)
			: (IMG_SIZE > IMG_PILE_SIZE && IMG_SIZE <= MASTODON_SIZE ? size_warning.substr(0, MSG_LEN - 17)
			: (IMG_SIZE > MASTODON_SIZE && IMG_SIZE <= POST_IMG_SIZE ? size_warning.substr(0, MSG_LEN - 27)
			: (IMG_SIZE > POST_IMG_SIZE && IMG_SIZE <= IMGBB_SIZE ? size_warning.substr(0, MSG_LEN - 38)
			: (IMG_SIZE > IMGBB_SIZE ? size_warning.substr(0, MSG_LEN - 45) : size_warning)))));

		if (pdv.data_size > REDDIT_SIZE && !pdv.reddit_opt) {
			std::cerr << size_warning << ".\n";
		}

		if (pdv.data_size <= REDDIT_OPTION_SIZE && pdv.reddit_opt) {
			std::cout << "\n**Warning**\n\nDue to the selection of the -r option, you can only post this file-embedded PNG image on Reddit.\n";
		}

		std::cout << "\nCreated file-embedded PNG image: \"" + pdv.data_file_name + "\" Size: \"" << pdv.Image_Vec.size() << " Bytes\"\n";

	} else {
		
		std::cout << "\nWriting decrypted file out to disk.\n";

		// Write out to disk the extracted data file.
		write_file_fs.write((char*)&pdv.Decrypted_Vec[0], pdv.Decrypted_Vec.size());

		std::cout << "\nSaved file: \"" + pdv.data_file_name + "\" Size: \"" << pdv.Decrypted_Vec.size() << " Bytes\"\n";

		pdv.Decrypted_Vec.clear();  // Clear vector. Important when decrypting/extracting multiple data-embedded image files.

	}
}

// Update values, such as chunk lengths, file sizes, CRC, etc. Write them into the relevant vector index locations. Overwrites previous values.
void Value_Updater(std::vector<BYTE>& vec, size_t value_insert_index, const size_t& VALUE, uint_fast8_t bits) {
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
PNG Data Vehicle (pdvrdt v1.6). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

A simple command-line tool used to embed and extract any file type via a PNG image file.  
Share your file-embedded image on the following compatible sites.  

Image size limit is platform dependant:-  

  Flickr (200MB), ImgBB (32MB), PostImage (24MB), *Reddit (20MB / with -r option), Mastodon (16MB), 
  *ImgPile (8MB), Imgur (5MB), *Reddit (1MB / without -r option), *Twitter (10KB).

Options: [-i] File insert mode.
	 [-x] File extract mode.
 	 [-r] Reddit option, used with -i insert mode (e.g. $ pdvrdt -i -r image_file.png music_file.mp3).

Using the -r option when embedding a data file increases the Reddit upload size limit from 1MB to 20MB.
The file-embedded PNG image created with the -r option can only be shared on Reddit and is incompatible
with the other platforms listed above.

*ImgPile - You must sign in to an account before sharing your file-embedded PNG image on this platform.
Sharing your image without logging in, your embedded file will not be preserved.

To embed and share larger files for Twitter (up to 5MB), please use pdvzip.

This program works on Linux and Windows.

For Flickr, ImgBB, PostImage, *Reddit (with -r option), Mastodon, ImgPile & Imgur,
the size limit is measured by the total size of your file-embedded PNG image file. 

For *Reddit (without -r option) & Twitter, the size limit is measured by the 
uncompressed size of the ICC Profile, where your data is stored.

Reddit (without -r option): 1,048,172 bytes is the uncompressed (zlib inflate) size limit for your data file.
404 bytes are used for the basic ICC Profile. (404 + 1048172 = 1,048,576 bytes [1MB]).

Twitter: 9,836 bytes is the uncompressed (zlib inflate) limit for your data file.
404 bytes are used for the basic ICC Profile (404 + 9836 = 10,240 bytes [10KB])

To maximise the amount of data you can embed in your image file for Reddit & Twitter, compress the data file to a ZIP or RAR file, etc. 
Make sure the compressed ZIP or RAR file does not exceed 1,048,172 bytes for Reddit or 9,836 bytes for Twitter. 

You can insert up to six files at a time (outputs one image per file).
You can also extract files from up to six images at a time.

)";
}
