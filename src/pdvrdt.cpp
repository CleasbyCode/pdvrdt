
//	PNG Data Vehicle for Flickr, ImgBB, PostImage, Mastodon, ImgPile (Account required), Imgur, Reddit & Twitter. (PDVRDT v1.6). 
//	Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023

//	Compile program (Linux)
//	$ g++ pdvrdt.cpp -O2 -lz -s -o pdvrdt
//	Run it
//	$ ./pdvrdt

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <zlib.h>

typedef unsigned char BYTE;

struct pdvStruct {
	std::vector<BYTE> ImageVec, FileVec, ProfileDataVec, ProfileChunkVec, EncryptedVec, DecryptedVec;
	std::vector<std::string> ChunkNameVec;
	std::string IMAGE_FILE, DATA_FILE;
	uint64_t IMAGE_SIZE{}, DATA_SIZE{};
	uint8_t Img_Value{}, Sub_Value{};
	bool Insert_File = false, Extract_File = false, Inflate_Data = false, Deflate_Data = false;
};

// Update values, such as chunk lengths, file sizes & other values. Write them into the relevant vector index locations. Overwrites previous values.
class ValueUpdater {
public:
	void Value(std::vector<BYTE>& vec, uint32_t valueInsertIndex, const uint32_t VALUE, uint8_t Bits) {
		while (Bits) vec[valueInsertIndex++] = (VALUE >> (Bits -= 8)) & 0xff;
	}
} *update;

void 
	// Open user image & data file and check file size requirements. Display error & exit program if any file fails to open or exceeds size limits.
	openFiles(char* [], pdvStruct& pdv),

	// Encrypt or decrypt user's data file.
	encryptDecrypt(pdvStruct& pdv),

	// Search and remove all unnecessary PNG chunks found before the first IDAT chunk.
	eraseChunks(pdvStruct& pdv),

	// Inflate or Deflate iCCP Profile chunk, which include user's encrypted data file.
	zlibCompress(std::vector<BYTE>&, bool),

	// Write out to file the embedded image or the extracted data file.
	writeOutFile(pdvStruct& pdv),

	// Display program infomation.
	displayInfo();

	// Code to compute CRC32 (for iCCP Profile chunk within this program).  https://www.w3.org/TR/2003/REC-PNG-20031110/#D-CRCAppendix 
uint32_t 
	Crc_Update(const uint32_t, BYTE*, const uint32_t),
	Crc(BYTE*, const uint32_t);

int main(int argc, char** argv) {

	pdvStruct pdv;

	if (argc == 2 && std::string(argv[1]) == "--info") {
		argc = 0;
		displayInfo();
	}
	else if (argc >= 4 && argc < 10 && std::string(argv[1]) == "-i") { // Insert file mode.
		pdv.Insert_File = true, pdv.Deflate_Data = true, pdv.Sub_Value = argc - 1, pdv.IMAGE_FILE = argv[2];
		argc -= 2;
		while (argc != 1) {
			pdv.Img_Value = argc, pdv.DATA_FILE = argv[3];
			openFiles(argv++, pdv);
			argc--;
		}
		argc = 1;
	}
	else if (argc >= 3 && argc < 9 && std::string(argv[1]) == "-x") { // Extract file mode.
		pdv.Extract_File = true;
		while (argc >= 3) {
			pdv.IMAGE_FILE = argv[2];
			openFiles(argv++, pdv);
			argc--;
		}
	}
	else {
		std::cerr << "\nUsage:\tpdvrdt -i <png-image>  <file(s)>\n\tpdvrdt -x <png-image(s)>\n\tpdvrdt --info\n\n";
		argc = 0;
	}
	if (argc != 0) {
		if (argc == 2) {
			std::cout << "\nComplete!\n\n";
		}
		else {
			std::cout << "\nComplete!\n\nYou can now post your data-embedded PNG image(s) to the relevant supported platforms.\n\n";
		}
	}
	return 0;
}

void openFiles(char* argv[], pdvStruct& pdv) {

	std::ifstream
		readImage(pdv.IMAGE_FILE, std::ios::binary),
		readFile(pdv.DATA_FILE, std::ios::binary);

	if (pdv.Insert_File && (!readImage || !readFile) || pdv.Extract_File && !readImage) {
		
		// Open file failure, display relevant error message and exit program.
		const std::string 
			READ_ERR_MSG = "\nRead Error: Unable to open/read file: ",
			ERR_MSG = !readImage ? READ_ERR_MSG + "\"" + pdv.IMAGE_FILE + "\"\n\n" : READ_ERR_MSG + "\"" + pdv.DATA_FILE + "\"\n\n";

		std::cerr << ERR_MSG;
		std::exit(EXIT_FAILURE);
	}

	std::string Start_Msg = pdv.Insert_File ? "\nInsert mode selected.\n\nReading files. Please wait...\n" : "\nExtract mode selected.\n\nReading embedded PNG image file. Please wait...\n";
	std::cout << Start_Msg;

	// Read PNG image (embedded or non-embedded) into vector "ImageVec".
	pdv.ImageVec.assign(std::istreambuf_iterator<char>(readImage), std::istreambuf_iterator<char>());

	const std::string
		PNG_SIG = "\x89PNG",
		GET_PNG_SIG{ pdv.ImageVec.begin(), pdv.ImageVec.begin() + PNG_SIG.length() };	// Get image header from vector. 

	// Make sure image has valid PNG header.
	if (GET_PNG_SIG != PNG_SIG) {

		// File requirements check failure, display relevant error message and exit program.
		std::cerr << "\nImage Error: File does not appear to be a valid PNG image.\n\n";
		std::exit(EXIT_FAILURE);
	}

	// Get size of image (embedded or non-embedded).
	pdv.IMAGE_SIZE = pdv.ImageVec.size();
	
	if (pdv.Extract_File) { // Extract mode. Inflate, decrypt and extract embedded data file from PNG image.

		uint8_t
			Profile_Chunk_Index = 0x25;	// iCCP chunk location within the PNG image file. 

		// Get iCCP Profile chunk signature from vector "ImageVec".
		const std::string
			PROFILE_SIG = "iCCPicc",
			GET_PROFILE_SIG{ pdv.ImageVec.begin() + Profile_Chunk_Index, pdv.ImageVec.begin() + Profile_Chunk_Index + PROFILE_SIG.length() };

		// Make sure an iCCP chunk exists somewhere within the PNG image.
		const auto PROFILE_POS = search(pdv.ImageVec.begin(), pdv.ImageVec.end(), PROFILE_SIG.begin(), PROFILE_SIG.end()) - pdv.ImageVec.begin();

		if (PROFILE_POS == pdv.IMAGE_SIZE) {

			// File requirement check failure, display relevant error message and exit program.
			std::cerr << "\nImage Error: No iCCP chunk found within this PNG file. This is not a pdvrdt data-embedded image.\n\n";
			std::exit(EXIT_FAILURE);
		}

		// Find and remove unwanted chunks. Some media sites add various PNG chunks to the downloaded image, 
		// which could cause issues with the expected location of the iCCP chunk.
		eraseChunks(pdv);

		if (GET_PROFILE_SIG != PROFILE_SIG)  {

			// File requirements check failure, display relevant error message and exit program.
			std::cerr << "\nPNG Error: Image file \"" << pdv.IMAGE_FILE << "\" does not appear to contain a valid iCCP chunk.\n\n";
			std::exit(EXIT_FAILURE);
		}

		std::cout << "\nFound compressed iCCP chunk.\n";

		const uint16_t
			PROFILE_CHUNK_SIZE_INDEX = 0x21,	// Start index location of 4 byte iCCP Profile chunk length field.
			DEFLATE_DATA_INDEX = 0x2E;		// Start index location of deflate data within ICCP Profile chunk. (78, 9C...).

		const uint32_t
			// Get iCCP Profile chunk length.
			PROFILE_CHUNK_SIZE = (pdv.ImageVec[PROFILE_CHUNK_SIZE_INDEX] << 24) | pdv.ImageVec[PROFILE_CHUNK_SIZE_INDEX + 1] << 16 | pdv.ImageVec[PROFILE_CHUNK_SIZE_INDEX + 2] << 8 | pdv.ImageVec[PROFILE_CHUNK_SIZE_INDEX + 3],
			DEFLATE_CHUNK_SIZE = PROFILE_CHUNK_SIZE - static_cast<uint32_t>(PROFILE_SIG.length() + 2); // + 2 is the two zero bytes after the iCCPicc chunk name.

		// From "ImageVec" vector index 0, erase n bytes so that start of the vector's contents is now the beginning of the deflate data (78,9C...).
		pdv.ImageVec.erase(pdv.ImageVec.begin(), pdv.ImageVec.begin() + DEFLATE_DATA_INDEX);

		// Erase all bytes of "ImageVec" after the end of the deflate data. Vector should now just contain the deflate data.
		pdv.ImageVec.erase(pdv.ImageVec.begin() + DEFLATE_CHUNK_SIZE, pdv.ImageVec.end());

		std::cout << "\nUncompressing iCCP chunk.\n";

		// Call function to inflate the profile chunk, which includes user's encrypted data file. 
		zlibCompress(pdv.ImageVec, pdv.Inflate_Data);

		const uint16_t
			FILENAME_LENGTH = pdv.ImageVec[0x64],
			FILENAME_START_INDEX = 0x65,
			PDV_SIG_START_INDEX = 0x191,
			DATA_FILE_START_INDEX = 0x194;

		// Get encrypted embedded filename from vector "ImageVec".
		pdv.DATA_FILE = { pdv.ImageVec.begin() + FILENAME_START_INDEX, pdv.ImageVec.begin() + FILENAME_START_INDEX + FILENAME_LENGTH };

		const std::string
			PDV_SIG = "PDV",
			// Get the pdvrdt signature from vector "ImageVec".
			GET_PDV_SIG{ pdv.ImageVec.begin() + PDV_SIG_START_INDEX, pdv.ImageVec.begin() + PDV_SIG_START_INDEX + PDV_SIG.length() };

		// Make sure this is a pdvrdt file-embedded image.
		if (GET_PDV_SIG != PDV_SIG) {
			std::cerr << "\nProfile Error: Profile does not appear to be a pdvrdt data-embedded profile.\n\n";
			std::exit(EXIT_FAILURE);
		}

		std::cout << "\nFound pdvrdt embedded data file.\n\nExtracting encrypted data file from iCCP chunk.\n";

		// Delete the profile from the inflated file, leaving just our encrypted data file.
		pdv.ImageVec.erase(pdv.ImageVec.begin(), pdv.ImageVec.begin() + DATA_FILE_START_INDEX);

		// This vector will be used to store the decrypted user's data file. 
		pdv.DecryptedVec.reserve(pdv.ImageVec.size());

		std::cout << "\nDecrypting extracted data file.\n";

		// Decrypt the contents of vector "ImageVec".
		encryptDecrypt(pdv);
	}

	else { // Insert File

		// Read-in user's data file and store it in vector "FileVec".
		pdv.FileVec.assign(std::istreambuf_iterator<char>(readFile), std::istreambuf_iterator<char>());

		// Get size of the user's data file.
		pdv.DATA_SIZE = pdv.FileVec.size();
		
		const uint32_t 
			MAX_FILE_SIZE_BYTES = 209715200,	// 200MB Max. (Flickr limit. The largest of the range of supported platforms).
			LAST_SLASH_POS = static_cast<uint32_t>(pdv.DATA_FILE.find_last_of("\\/"));

		if ((pdv.IMAGE_SIZE + pdv.DATA_SIZE + 0x194) > MAX_FILE_SIZE_BYTES) {

			// File size check failure, display relevant error message and exit program.
			std::cerr << "\nImage Size Error: PNG image (including embedded data file) must not exceed 200MB.\n\n";
			std::exit(EXIT_FAILURE);
		}

		// We don't want "./" or ".\" characters at the start of the filename (user's data file), which we will store in the profile. 
		if (LAST_SLASH_POS <= pdv.DATA_FILE.length()) {
			const std::string_view NO_SLASH_NAME(pdv.DATA_FILE.c_str() + (LAST_SLASH_POS + 1), pdv.DATA_FILE.length() - (LAST_SLASH_POS + 1));
			pdv.DATA_FILE = NO_SLASH_NAME;
		}

		pdv.ProfileDataVec.reserve(pdv.DATA_SIZE + 404);
		pdv.ProfileChunkVec.reserve(pdv.DATA_SIZE + 429);
		pdv.EncryptedVec.reserve(pdv.DATA_SIZE);

		// The first 401 bytes of the vector "ProfileDataVec" contains the basic profile, followed by a 3-byte signature "PDV". 404 bytes total.
		// The length value of the user's data filename and the encrypted filename are stored within this profile.
		// After the user's data file has been encrypted and compressed, it is inserted and stored at the end of this profile.
		pdv.ProfileDataVec = {
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
			// The above vector "ProfileDataVec", containing the basic profile, that will later be compressed (deflate), 
			// and will also contain the encrypted & compressed user's data file, will itself be inserted into this vector "ProfileChunkVec". 
			// The vector initially contains just the PNG iCCP chunk header (chunk length field, name and crc value field).
			pdv.ProfileChunkVec = {
			0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		};

		// Find and remove unwanted PNG chunks.
		eraseChunks(pdv);
	}
}

void encryptDecrypt(pdvStruct& pdv) {

	const std::string XOR_KEY = "\xFC\xD8\xF9\xE2\x9F\x7E";	// String used to XOR encrypt/decrypt the filename of user's data file.

	uint32_t 
		File_Size = pdv.Insert_File ? static_cast<uint32_t>(pdv.FileVec.size()) : static_cast<uint32_t>(pdv.ImageVec.size()),	 // File size of user's data file.
		Index_Pos = 0;

	const uint8_t
		MAX_LENGTH_FILENAME = 23,
		NAME_LENGTH = static_cast<uint8_t>(pdv.DATA_FILE.length()),	// Character length of user's data file name.
		XOR_KEY_LENGTH = static_cast<uint8_t>(XOR_KEY.length()),
		XOR_KEY_START_POS = 0,
		NAME_KEY_START_POS = 0;

	// Make sure character length of filename does not exceed set maximum.
	if (NAME_LENGTH > MAX_LENGTH_FILENAME) {
		std::cerr << "\nFile Error: Filename length of your data file (" + std::to_string(NAME_LENGTH) + " characters) is too long.\n"
			"\nFor compatibility requirements, your filename must be under 24 characters.\nPlease try again with a shorter filename.\n\n";
		std::exit(EXIT_FAILURE);
	}

	const std::string INPUT_NAME = pdv.DATA_FILE;

	std::string Output_Name;

	uint8_t
		Xor_Key_Pos = XOR_KEY_START_POS,	// Character position variable for XOR_KEY string.
		Name_Key_Pos = NAME_KEY_START_POS;	// Character position variable for filename string (Out_Name / In_Name).

	if (NAME_LENGTH > File_Size) {		// File size needs to be greater than filename length.
		std::cerr << "\nFile Size Error: File size is too small. File size must be greater than the length of the file name.\n\n";
		std::exit(EXIT_FAILURE);
	}

	// XOR encrypt or decrypt filename and data file.
	while (File_Size > Index_Pos) {

		if (Index_Pos >= NAME_LENGTH) {
			Name_Key_Pos = Name_Key_Pos > NAME_LENGTH ? NAME_KEY_START_POS : Name_Key_Pos;	 // Reset filename character position to the start if it's reached last character.
		}
		else {
			Xor_Key_Pos = Xor_Key_Pos > XOR_KEY_LENGTH ? XOR_KEY_START_POS : Xor_Key_Pos;	// Reset XOR_KEY position to the start if it has reached last character.
			Output_Name += INPUT_NAME[Index_Pos] ^ XOR_KEY[Xor_Key_Pos++];			// XOR each character of filename against characters of XOR_KEY string. Store output characters in "outName".
													// Depending on Mode, filename is either encrypted or decrypted.
		}

		if (pdv.Insert_File) {
			// Encrypt data file. XOR each byte of the data file within vector "FileVec" against each character of the encrypted filename, "Output_Name". 
			// Store encrypted output in vector "ProfileDataVec".
			pdv.ProfileDataVec.emplace_back(pdv.FileVec[Index_Pos++] ^ Output_Name[Name_Key_Pos++]);
		}
		else {
			// Decrypt data file: XOR each byte of the data file within vector "ImageVec" against each character of the encrypted filename, "INPUT_NAME". 
			// Store decrypted output in vector "DecryptedVec".
			pdv.DecryptedVec.emplace_back(pdv.ImageVec[Index_Pos++] ^ INPUT_NAME[Name_Key_Pos++]);
		}
	}

	if (pdv.Insert_File) {

		uint8_t
			Profile_DataVec_Size_Index = 0x00,	// Start index location of the compressed profile's 4 byte length field.
			Profile_Chunk_Size_Index = 0x00,	// Start index of the PNG iCCP chunk's 4 byte length field.
			Profile_Name_Length_Index = 0x64,	// Index location inside the compressed profile to store the length value of the user's data filename (1 byte).
			Profile_Name_Index = 0x65,		// Start index location inside the compressed profile to store the encrypted filename for the user's embedded data file.
			Bits = 32;				// Value used in the "updateValue" function.

		// Insert the character length value of the filename into the profile,
		// We need to know how many characters to read when we later retrieve the filename from the profile, during file extraction.
		pdv.ProfileDataVec[Profile_Name_Length_Index] = NAME_LENGTH;

		// Make space for the filename by removing equivalent length of characters from profile.
		pdv.ProfileDataVec.erase(pdv.ProfileDataVec.begin() + Profile_Name_Index, pdv.ProfileDataVec.begin() + NAME_LENGTH + Profile_Name_Index);

		// Insert the encrypted filename into the profile.
		pdv.ProfileDataVec.insert(pdv.ProfileDataVec.begin() + Profile_Name_Index, Output_Name.begin(), Output_Name.end());

		// Update internal profile size.
		update->Value(pdv.ProfileDataVec, Profile_DataVec_Size_Index, static_cast<uint32_t>(pdv.ProfileDataVec.size()), Bits);

		std::cout << "\nCompressing data file.\n";

		// Call function to deflate/compress the contents of vector "ProfileDataVec" (profile with user's encrypted data file).
		zlibCompress(pdv.ProfileDataVec, pdv.Deflate_Data);

		const uint32_t
			PROFILE_DEFLATE_SIZE = static_cast<uint32_t>(pdv.ProfileDataVec.size()),
			PROFILE_CHUNK_SIZE = PROFILE_DEFLATE_SIZE + 9; // "iCCPicc\x00\x00" (+ 9 bytes).

		const uint8_t
			PROFILE_DATA_INSERT_INDEX = 0x0D,   // Index location within ProfileChunkVec to insert the deflate contents of ProfileDataVec. 		   
			PROFILE_CHUNK_INSERT_INDEX = 0x21,  // Index location within ImageVec (image file) to insert ProfileChunkVec (iCCP chunk).
			PROFILE_CHUNK_START_INDEX = 0x04;

		// Insert the deflate/compressed iCCP chunk (with user's encrypted/compressed data file) into vector "ProfileChunkVec", begining at index location 21.
		pdv.ProfileChunkVec.insert((pdv.ProfileChunkVec.begin() + PROFILE_DATA_INSERT_INDEX), pdv.ProfileDataVec.begin(), pdv.ProfileDataVec.end());

		// Call function to insert new chunk length value into "ProfileChunkVec" vector's index length field. 
		update->Value(pdv.ProfileChunkVec, Profile_Chunk_Size_Index, PROFILE_DEFLATE_SIZE + 5, Bits);

		// Pass these two values (PROFILE_CHUNK_START_INDEX and PROFILE_CHUNK_SIZE) to the CRC fuction to get correct iCCP chunk CRC.
		const uint32_t PROFILE_CHUNK_CRC = Crc(&pdv.ProfileChunkVec[PROFILE_CHUNK_START_INDEX], PROFILE_CHUNK_SIZE);

		// Index location for the 4-byte CRC field of the iCCP chunk.
		uint32_t Profile_Crc_Insert_Index = PROFILE_CHUNK_START_INDEX + PROFILE_CHUNK_SIZE;

		// Call function to insert iCCP chunk CRC value into "ProfileChunkVec" vector's CRC index field. 
		update->Value(pdv.ProfileChunkVec, Profile_Crc_Insert_Index, PROFILE_CHUNK_CRC, Bits);

		std::cout << "\nEmbedding data file within the iCCP chunk of the PNG image.\n";

		// Insert contents of vector "ProfileChunkVec" into vector "ImageVec", combining iCCP chunk (compressed profile & user's data file) with PNG image.
		pdv.ImageVec.insert((pdv.ImageVec.begin() + PROFILE_CHUNK_INSERT_INDEX), pdv.ProfileChunkVec.begin(), pdv.ProfileChunkVec.end());

		// If we embed multiple data files (Max. 6), each outputted image will be differentiated by a number in the filename, e.g. jdv_img1.jpg, jdv_img2.jpg, jdv_img3.jpg.
		const std::string DIFF_VALUE = std::to_string(pdv.Sub_Value - pdv.Img_Value);
		pdv.DATA_FILE = "pdv_img" + DIFF_VALUE + ".png";

		std::cout << "\nWriting data-embedded PNG image out to disk.\n";

		// Write out to file the PNG image with the embedded (encrypted/compressed) user's data file.
		writeOutFile(pdv);
	}
	else { // Extract Mode.

		// Update string variable with the decrypted filename.
		pdv.DATA_FILE = Output_Name;

		// Write the extracted (inflated / decrypted) data file out to disk.
		writeOutFile(pdv);
	}
}

void zlibCompress(std::vector<BYTE>& Vec, bool Deflate_Data) {

	// zlib function, see https://zlib.net/

	std::vector <BYTE> Buffer;

	const uint32_t 
		VEC_SIZE = static_cast<uint32_t>(Vec.size()),
		BUFSIZE = 256 * 1024;

	Buffer.reserve(VEC_SIZE);
	
	BYTE* Temp_Buffer{ new BYTE[BUFSIZE] };

	z_stream strm;
	strm.zalloc = 0;
	strm.zfree = 0;
	strm.next_in = Vec.data();
	strm.avail_in = VEC_SIZE;
	strm.next_out = Temp_Buffer;
	strm.avail_out = BUFSIZE;

	if (Deflate_Data) {
		deflateInit(&strm, 6); // Compression level 6 (78, 9C...)
	}
	else {
		inflateInit(&strm);
	}

	while (strm.avail_in)
	{
		if (Deflate_Data) {
			deflate(&strm, Z_NO_FLUSH);	
		}
		else {
			inflate(&strm, Z_NO_FLUSH);
		}

		if (!strm.avail_out)
		{
			Buffer.insert(Buffer.end(), Temp_Buffer, Temp_Buffer + BUFSIZE);
			strm.next_out = Temp_Buffer;
			strm.avail_out = BUFSIZE;
		}
		else
			break;
	}
	if (Deflate_Data) {
		deflate(&strm, Z_FINISH);
		Buffer.insert(Buffer.end(), Temp_Buffer, Temp_Buffer + BUFSIZE - strm.avail_out);
		deflateEnd(&strm);
	}
	else {
		inflate(&strm, Z_FINISH);
		Buffer.insert(Buffer.end(), Temp_Buffer, Temp_Buffer + BUFSIZE - strm.avail_out);
		inflateEnd(&strm);
	}

	Vec.clear();
	Vec.swap(Buffer);
	delete[] Temp_Buffer;
	Buffer.clear();
}

void eraseChunks(pdvStruct& pdv) {

	pdv.ChunkNameVec = { "IDAT", "bKGD", "cHRM", "sRGB", "hIST", "iCCP", "pHYs", "sBIT", "gAMA", "sPLT", "tIME", "tRNS", "tEXt", "iTXt", "zTXt" };

	if (pdv.Extract_File) {  // If extracting data, we need to keep the iCCP chunk, so rename it to prevent find/removal. 
				 // Also change the first (0) name element to "iCCP", so that it becomes our search limit marker. See for-loop below.
		pdv.ChunkNameVec[5] = "pdVRdt_KEeP_Me_PDvrDT";
		pdv.ChunkNameVec[0] = "iCCP";
	}

	for (uint8_t Chunk_Name_Elements = static_cast<uint8_t>(pdv.ChunkNameVec.size() -1); Chunk_Name_Elements > 0; Chunk_Name_Elements--) {
		// If Inserting, get first IDAT chunk index location. Don't remove chunks after this search limit location.
		// If Extracting, get iCCP chunk index location. Don't remove chunks after this search limit location.
		uint32_t
			Search_Limit_Index = static_cast<uint32_t>(search(pdv.ImageVec.begin(), pdv.ImageVec.end(), pdv.ChunkNameVec[0].begin(), pdv.ChunkNameVec[0].end()) - pdv.ImageVec.begin()),
			// From last to first, search and get index location of each chunk to remove.
			Chunk_Found_Index = static_cast<uint32_t>(search(pdv.ImageVec.begin(), pdv.ImageVec.end(), pdv.ChunkNameVec[Chunk_Name_Elements].begin(), pdv.ChunkNameVec[Chunk_Name_Elements].end()) - pdv.ImageVec.begin()) - 4;

		// If found chunk is located before search limit location, we can remove it.
		if (Search_Limit_Index > Chunk_Found_Index) {
			uint32_t Chunk_Size = (pdv.ImageVec[Chunk_Found_Index + 1] << 16) | pdv.ImageVec[Chunk_Found_Index + 2] << 8 | pdv.ImageVec[Chunk_Found_Index + 3];
			pdv.ImageVec.erase(pdv.ImageVec.begin() + Chunk_Found_Index, pdv.ImageVec.begin() + Chunk_Found_Index + (Chunk_Size + 12));
			Chunk_Name_Elements++; // Increment chunk name element value so that we search again for the same chunk, in case of multiple occurrences.
		}
	}

	// Now that we have removed unwanted chunks, encrypt the user's data file (if inserting file).
	if (pdv.Insert_File) {

		std::cout << "\nEncrypting data file.\n";

		encryptDecrypt(pdv);
	}
}

uint32_t Crc_Update(const uint32_t Crc, BYTE* buf, const uint32_t len)
{
		// Table of CRCs of all 8-bit messages.
		std::vector<uint32_t> CrcTableVec =
		{
			0x00,	    0x77073096, 0xee0e612c, 0x990951ba, 0x76dc419,  0x706af48f, 0xe963a535, 0x9e6495a3, 0xedb8832,  0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x9b64c2b,
			0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856, 0x646ba8c0,
			0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd,
			0xa50ab56b, 0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59, 0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
			0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190,
			0x1db7106,  0x98d220bc, 0xefd5102a, 0x71b18589, 0x6b6b51f,  0x9fbfe4a5, 0xe8b8d433, 0x7807c9a2, 0xf00f934,  0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x86d3d2d,
			0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea,
			0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65, 0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
			0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525,
			0x206f85b3, 0xb966d409, 0xce61e49f, 0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6,
			0x3b6e20c,  0x74b1d29a, 0xead54739, 0x9dd277af, 0x4db2615,  0x73dc1683, 0xe3630b12, 0x94643b84, 0xd6d6a3e,  0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0xa00ae27,
			0x7d079eb1, 0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
			0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b, 0xd80d2bda,
			0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703,
			0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d, 0x9b64c2b0, 0xec63f226, 0x756aa39c,
			0x26d930a,  0x9c0906a9, 0xeb0e363f, 0x72076785, 0x5005713,  0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0xcb61b38,  0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0xbdbdf21,
			0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777, 0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff,
			0xf862ae69, 0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
			0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9, 0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729,
			0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d };

	// Update a running CRC with the bytes buf[0..len - 1] the CRC should be initialized to all 1's, 
	// and the transmitted value is the 1's complement of the final running CRC (see the crc() routine below).
	uint32_t c = Crc;

	for (uint32_t n{}; n < len; n++) {
		c = CrcTableVec[(c ^ buf[n]) & 0xff] ^ (c >> 8);
	}
	
	CrcTableVec.clear();
	return c;
}

// Return the CRC of the bytes buf[0..len-1].
uint32_t Crc(BYTE* buf, const uint32_t len)
{
	return Crc_Update(0xffffffffL, buf, len) ^ 0xffffffffL;
}

void writeOutFile(pdvStruct& pdv) {

	std::ofstream writeFile(pdv.DATA_FILE, std::ios::binary);

	if (!writeFile) {
		std::cerr << "\nWrite Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}

	if (pdv.Insert_File) {

		// Write out to disk the PNG image embedded with the user's encrypted/compressed data file.
		writeFile.write((char*)&pdv.ImageVec[0], pdv.ImageVec.size());

		std::string Size_Warning =
			"\n**Warning**\n\nDue to the file size of your data-embedded PNG image,\nyou will only be able to share this image on the following platforms:\n\n"
			"Flickr, ImgBB, PostImage, Mastodon, ImgPile & Imgur";

		const uint8_t MSG_LEN = static_cast<uint8_t>(Size_Warning.length());

		const uint32_t
			IMG_SIZE = static_cast<uint32_t>(pdv.ImageVec.size()),
			// Twitter 9.5KB. While not officially supported because of the tiny size requirement, if your data file is this size 
			// (9.5KB, 9836 bytes) or lower with image dimensions 900x900 or less (PNG-32/24) 4096x4096 or less (PNG-8), 
			// then you should be able to use Twitter to share your data-embedded image.
			REDDIT_SIZE =	 1048172,	// Just under 1MB.
			IMGUR_SIZE =	 5242880,	// 5MB
			IMG_PILE_SIZE =  8388608,	// 8MB
			MASTODON_SIZE =  16777216,	// 16MB
			POST_IMG_SIZE =	 25165824,	// 24MB
			IMGBB_SIZE =	 33554432;	// 32MB
			// Flickr is 200MB, this programs max size, no need to make a variable for it.
		
		Size_Warning = (IMG_SIZE > IMGUR_SIZE && IMG_SIZE <= IMG_PILE_SIZE ? Size_Warning.substr(0, MSG_LEN - 8)
						: (IMG_SIZE > IMG_PILE_SIZE && IMG_SIZE <= MASTODON_SIZE ? Size_Warning.substr(0, MSG_LEN - 17)
						: (IMG_SIZE > MASTODON_SIZE && IMG_SIZE <= POST_IMG_SIZE ? Size_Warning.substr(0, MSG_LEN - 27)
						: (IMG_SIZE > POST_IMG_SIZE && IMG_SIZE <= IMGBB_SIZE ? Size_Warning.substr(0, MSG_LEN - 38)
						: (IMG_SIZE > IMGBB_SIZE ? Size_Warning.substr(0, MSG_LEN - 45) : Size_Warning)))));

		if (pdv.DATA_SIZE > REDDIT_SIZE) {
			std::cerr << Size_Warning << ".\n";
		}

		std::cout << "\nCreated data-embedded PNG image: \"" + pdv.DATA_FILE + "\" Size: \"" << pdv.ImageVec.size() << " Bytes\".\n";

		pdv.EncryptedVec.clear();  // Clear vector. Important when encrypting multiple, separate files. 
	}
	else {
		
		std::cout << "\nWriting decrypted data file out to disk.\n";

		// Write out to disk the extracted data file.
		writeFile.write((char*)&pdv.DecryptedVec[0], pdv.DecryptedVec.size());

		std::cout << "\nSaved file: \"" + pdv.DATA_FILE + "\" Size: \"" << pdv.DecryptedVec.size() << " Bytes\"\n";

		pdv.DecryptedVec.clear();  // Clear vector. Important when decrypting/extracting multiple data-embedded image files.

	}
}

void displayInfo() {

	std::cout << R"(
PNG Data Vehicle (pdvrdt v1.6). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

A simple command-line tool to embed and extract any file type via a PNG image file.  
Share your data-embedded image on the following compatible sites.  

Image size limit is platform dependant:-  

* Flickr (200MB), ImgBB (32MB), PostImage (24MB), Mastodon (16MB), 
* ImgPile (8MB Account required), Imgur (5MB), *Reddit (1MB Desktop/Browser only), *Twitter (10KB).

To embed larger files for Twitter (5MB max.), please use pdvzip.
To embed larger files for Reddit (20MB max.), please use jdvrif. You can also use jdvrif for Mastodon (16MB max).

This program works on Linux and Windows.

For Flickr, Mastodon, ImgBB, PostImage, Mastodon, ImgPile & Imgur,
the size limit is measured by the total size of your data-embedded PNG image file. 

For Reddit & Twitter, the size limit is measured by the uncompressed size of the ICC Profile, where your data is stored.

Reddit: 1,048,172 bytes is the uncompressed (zlib inflate) size limit for your data file.
404 bytes are used for the basic ICC Profile. (404 + 1048172 = 1,048,576 bytes [1MB]).

Twitter: 9,836 bytes is the uncompressed (zlib inflate) limit for your data file.
404 bytes are used for the basic ICC Profile (404 + 9836 = 10,240 bytes [10KB])

To maximise the amount of data you can embed in your image file for Reddit & Twitter, compress the data file to a ZIP or RAR file, etc. 
Make sure the compressed ZIP or RAR file does not exceed 1,048,172 bytes for Reddit or 9,836 bytes for Twitter. 

You can insert up to six files at a time (outputs one image per file).
You can also extract files from up to six images at a time.

)";
}
