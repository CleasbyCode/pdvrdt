//	PNG Data Vehicle for Reddit, (PDVRDT v1.4). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023
//	Compile program (Linux)
//	$ g++ pdvrdt.cpp -lz -s -o pdvrdt
//	Run it
//	$ ./pdvrdt

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
#include <zlib.h>

namespace fs = std::filesystem;

typedef unsigned char BYTE;
typedef unsigned short SBYTE;

struct pdvStruct {
	std::vector<BYTE> ImageVec, FileVec, ProfileDataVec, ProfileChunkVec, EncryptedVec, DecryptedVec;
	std::string IMAGE_FILE, DATA_FILE, MODE;
	size_t IMAGE_SIZE{}, DATA_SIZE{};
	SBYTE imgVal{}, subVal{};
};

// Update values, such as chunk lengths, file sizes & other values. Write them into the relevant vector index locations. Overwrites previous values.
class ValueUpdater {
public:
	void Value(std::vector<BYTE>& vec, size_t valueInsertIndex, const size_t VALUE, SBYTE bits) {
			while (bits) vec[valueInsertIndex++] = (VALUE >> (bits -= 8)) & 0xff;
		}
} *update;

// Open user image & data file and check file size requirements. Display error & exit program if any file fails to open or exceeds size limits.
void openFiles(char* [], pdvStruct& pdv);

// Encrypt / decrypt user's data file.
void encryptDecrypt(pdvStruct& pdv);

// Search and remove all unnecessary PNG chunks found before the first IDAT chunk.
void eraseChunks(std::vector<BYTE>& Vec, pdvStruct& pdv);

// Inflate / Deflate iCCP Profile chunk, which include user's encrypted data file.
void inflateDeflate(std::vector<BYTE>&, bool);

// Write out to file the embedded image or the extracted data file.
void writeOutFile(pdvStruct& pdv);

// Display program infomation
void displayInfo();

// Code to compute CRC32 (for iCCP Profile chunk within this program) was taken from: https://www.w3.org/TR/2003/REC-PNG-20031110/#D-CRCAppendix 
size_t crcUpdate(const size_t&, BYTE*, const size_t&);
size_t crc(BYTE*, const size_t&);

int main(int argc, char** argv) {
	
	pdvStruct pdv;

	if (argc == 2 && std::string(argv[1]) == "--info") {
		argc = 0;
		displayInfo();
	}
	else if (argc >= 4 && argc < 10 && std::string(argv[1]) == "-i") { // Insert file mode.
		pdv.MODE = argv[1], pdv.subVal = argc - 1, pdv.IMAGE_FILE = argv[2];
		argc -= 2;
		while (argc != 1) {
			pdv.imgVal = argc, pdv.DATA_FILE = argv[3];
			openFiles(argv++, pdv);
			argc--;
		}
		argc = 1;
	}
	else if (argc >= 3 && argc < 9 && std::string(argv[1]) == "-x") { // Extract file mode.
		pdv.MODE = argv[1];
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
			std::cout << "\nComplete!\n\nYou can now post your \"file-embedded\" PNG image(s) to the relevant supported platforms.\n\n";
		}
	}
	return 0;
}

void openFiles(char* argv[], pdvStruct& pdv) {

	std::ifstream
		readImage(pdv.IMAGE_FILE, std::ios::binary),
		readFile(pdv.DATA_FILE, std::ios::binary);

	if (pdv.MODE == "-i" && (!readImage || !readFile) || pdv.MODE == "-x" && !readImage) {

		const std::string READ_ERR_MSG = "\nRead Error: Unable to open/read file: ";

		// Open file failure, display relevant error message and exit program.
		const std::string ERR_MSG = !readImage ? READ_ERR_MSG + "\"" + pdv.IMAGE_FILE + "\"\n\n" : READ_ERR_MSG + "\"" + pdv.DATA_FILE + "\"\n\n";

		std::cerr << ERR_MSG;
		std::exit(EXIT_FAILURE);
	}
	
	// Get size of image (embedded / non-embedded).
	pdv.IMAGE_SIZE = fs::file_size(pdv.IMAGE_FILE);

	pdv.ImageVec.reserve(16777216);

	// Read PNG image (embedded / non-embedded) into vector "ImageVec".
	pdv.ImageVec.assign(std::istreambuf_iterator<char>(readImage), std::istreambuf_iterator<char>());

	if (pdv.MODE == "-x") { // Extract mode. Inflate, decrypt and extract embedded data file from PNG image.
		
		// Find and remove unwanted chunks. Some media sites add chunks to the downloaded image.
		eraseChunks(pdv.ImageVec, pdv);

		// When Reddit encodes the uploaded PNG image, if it is PNG-8, it will write the PLTE chunk BEFORE the iCCP Profile chunk. 
		// We need to check for this and make adjustments so as to remain correctly aligned with the file.
		SBYTE
			plteChunkSize = 0,		// If no PLTE chunk (PNG-32/24), then this value remains 0.
			plteChunkSizeIndex = 33,	// If PNG-8, location of PLTE chunk length field.
			chunkIndex = 37;	  	// If PNG-8, chunk index location of PLTE chunk. If PNG-32/24, chunk index of iCCP Profile. 

		const std::string
			PLTE_SIG = "PLTE",
			PLTE_GET_SIG { pdv.ImageVec.begin() + chunkIndex, pdv.ImageVec.begin() + chunkIndex + PLTE_SIG.length() };

		if (PLTE_GET_SIG == PLTE_SIG) {
			// Get length of the PLTE chunk
			plteChunkSize = 12 + (pdv.ImageVec[plteChunkSizeIndex + 1] << 16) | pdv.ImageVec[plteChunkSizeIndex + 2] << 8 | pdv.ImageVec[plteChunkSizeIndex + 3];
		}

		// Get iCCP Profile chunk signature from vector.
		const std::string
			PROFILE_COMMON_SIG = "iCCPICC\x20Profile",
			PROFILE_MASTODON_SIG = "iCCPicc", // Mastodon saves images with a shorter profile name. 		
			GET_PROFILE_COMMON_SIG{ pdv.ImageVec.begin() + chunkIndex + plteChunkSize, pdv.ImageVec.begin() + chunkIndex + plteChunkSize + PROFILE_COMMON_SIG.length() },
			GET_PROFILE_MASTODON_SIG{ pdv.ImageVec.begin() + chunkIndex + plteChunkSize, pdv.ImageVec.begin() + chunkIndex + plteChunkSize + PROFILE_MASTODON_SIG.length() };

		bool PROFILE_MASTODON = false;

		if (GET_PROFILE_COMMON_SIG != PROFILE_COMMON_SIG) {
			
			if (GET_PROFILE_MASTODON_SIG == PROFILE_MASTODON_SIG) {

				// If a shorter Mastodon profile signature is found, remove it and replace it with the common one. 
				// This keeps the file in correct alignment for inflating, decrypting and extracting the data file.

				pdv.ImageVec.erase(pdv.ImageVec.begin() + chunkIndex + plteChunkSize, pdv.ImageVec.begin() + chunkIndex + plteChunkSize + PROFILE_MASTODON_SIG.length());
				pdv.ImageVec.insert(pdv.ImageVec.begin() + chunkIndex + plteChunkSize, PROFILE_COMMON_SIG.begin(), PROFILE_COMMON_SIG.end());
				PROFILE_MASTODON = true;
			}
			else {

				// File requirements check failure, display relevant error message and exit program.
				std::cerr << "\nPNG Error: Image file \"" << pdv.IMAGE_FILE << "\" does not appear to contain a valid iCCP profile.\n\n";
				std::exit(EXIT_FAILURE);
			}
		}

		int
			profileChunkSizeIndex = 33 + plteChunkSize, // Start index location of 4 byte iCCP Profile chunk length field.
			// Get iCCP Profile chunk length.
			profileChunkSize = (pdv.ImageVec[profileChunkSizeIndex] << 24) | pdv.ImageVec[profileChunkSizeIndex + 1] << 16 | pdv.ImageVec[profileChunkSizeIndex + 2] << 8 | pdv.ImageVec[profileChunkSizeIndex + 3],
			deflateDataIndex = 54 + plteChunkSize, // Start index location of deflate data within iCCP Profile chunk. (78, 9C...).
			deflateChunkSize = profileChunkSize - 13;

		if (PROFILE_MASTODON) {
			// Because Mastodon wrote a shorter profile name, it also changed the chunk size.  
			// We also need to update these size fields.
			profileChunkSize += 8;
			deflateChunkSize = profileChunkSize - 13;
		}

		// From "ImageVec" vector index 0, erase n bytes so that start of the vector's contents is now the beginning of the deflate data (78,9C...).
		pdv.ImageVec.erase(pdv.ImageVec.begin(), pdv.ImageVec.begin() + deflateDataIndex);

		// Erase all bytes of "ImageVec" after the end of the deflate data. Vector should now just contain the deflate data.
		pdv.ImageVec.erase(pdv.ImageVec.begin() + deflateChunkSize, pdv.ImageVec.end());

		bool inflate = true; // Inflate (uncompress).

		// Call function to inflate the profile chunk, which includes user's encrypted data file. 
		inflateDeflate(pdv.ImageVec, inflate);
		
		const int
			FILENAME_LENGTH = pdv.ImageVec[100],
			FILENAME_START_INDEX = 101,
			PDV_SIG_START_INDEX = 401,
			DATA_FILE_START_INDEX = 404;
		
		// Get encrypted embedded filename from vector "ImageVec".
		pdv.DATA_FILE = { pdv.ImageVec.begin() + FILENAME_START_INDEX, pdv.ImageVec.begin() + FILENAME_START_INDEX + FILENAME_LENGTH };	
	
		const std::string
			PDV_SIG = "pdv",
			// Get the pdvrdt signature from vector "ImageVec".
			PDV_GET_SIG{ pdv.ImageVec.begin() + PDV_SIG_START_INDEX, pdv.ImageVec.begin() + PDV_SIG_START_INDEX + PDV_SIG.length() };
		
		// Make sure this is a pdvrdt file-embedded image.
		if (PDV_GET_SIG != PDV_SIG) {
			std::cerr << "\nProfile Error: Profile does not appear to be a PDVRDT \"file-embedded\" profile.\n\n";
			std::exit(EXIT_FAILURE);
		}
		
		// Delete the profile from the inflated file, leaving just our encrypted data file.
		pdv.ImageVec.erase(pdv.ImageVec.begin(), pdv.ImageVec.begin() + DATA_FILE_START_INDEX);

		// This vector will be used to store the decrypted user's data file. 
		pdv.DATA_SIZE = pdv.ImageVec.size();
		pdv.DecryptedVec.reserve(pdv.DATA_SIZE);
		
		// Decrypt the contents of vector "ImageVec".
		encryptDecrypt(pdv);

	}
	else { // "-i" Insert Mode

		// Get size of the user's data file.
		pdv.DATA_SIZE = fs::file_size(pdv.DATA_FILE);

		const int MAX_FILE_SIZE_BYTES = 16777216;	// 16MB Max. size for image + data File. (Mastodon image size limit).

		if ((pdv.IMAGE_SIZE + pdv.DATA_SIZE) > MAX_FILE_SIZE_BYTES) {
			// File size check failure, display relevant error message and exit program.
			std::cerr << "\nImage Size Error: PNG image (+including embedded data file) must not exceed 16MB.\n\n";
			std::exit(EXIT_FAILURE);
		}

		pdv.ProfileDataVec.reserve(pdv.DATA_SIZE);
		pdv.ProfileChunkVec.reserve(pdv.DATA_SIZE + 1024);
		pdv.FileVec.reserve(pdv.DATA_SIZE);

		// The first 401 bytes of the vector "ProfileDataVec" contains the basic profile, followed by a 3-byte signature "pdv". 404 bytes total.
		// The length value of the user's data filename and the encrypted filename are stored within this profile.
		// After the user's data file has been encrypted and compressed, it is inserted and stored at the end of this profile.

		pdv.ProfileDataVec = {
			0x00, 0x00, 0x00, 0x00, 0x6c, 0x63, 0x6d, 0x73, 0x02, 0x10, 0x00, 0x00,
			0x6D, 0x6E, 0x74, 0x72, 0x52, 0x47, 0x42, 0x20, 0x58, 0x59, 0x5A, 0x20,
			0x07, 0xE2, 0x00, 0x03, 0x00, 0x14, 0x00, 0x09, 0x00, 0x0E, 0x00, 0x1D,
			0x61, 0x63, 0x73, 0x70, 0x4D, 0x53, 0x46, 0x54, 0x00, 0x00, 0x00, 0x00,
			0x73, 0x61, 0x77, 0x73, 0x63, 0x74, 0x72, 0x6C, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF6, 0xD6,
			0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0xD3, 0x2D, 0x68, 0x61, 0x6E, 0x64,
			0xEB, 0x77, 0x1F, 0x3C, 0xAA, 0x53, 0x51, 0x02, 0xE9, 0x3E, 0x28, 0x6C,
			0x91, 0x46, 0xAE, 0x57, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09,
			0x64, 0x65, 0x73, 0x63, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x1C,
			0x77, 0x74, 0x70, 0x74, 0x00, 0x00, 0x01, 0x0C, 0x00, 0x00, 0x00, 0x14,
			0x72, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x20, 0x00, 0x00, 0x00, 0x14,
			0x67, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x34, 0x00, 0x00, 0x00, 0x14,
			0x62, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x48, 0x00, 0x00, 0x00, 0x14,
			0x72, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0x5C, 0x00, 0x00, 0x00, 0x34,
			0x67, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0x5C, 0x00, 0x00, 0x00, 0x34,
			0x62, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0x5C, 0x00, 0x00, 0x00, 0x34,
			0x63, 0x70, 0x72, 0x74, 0x00, 0x00, 0x01, 0x90, 0x00, 0x00, 0x00, 0x01,
			0x64, 0x65, 0x73, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
			0x6E, 0x52, 0x47, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0xF3, 0x54, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x16, 0xC9,
			0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6F, 0xA0,
			0x00, 0x00, 0x38, 0xF2, 0x00, 0x00, 0x03, 0x8F, 0x58, 0x59, 0x5A, 0x20,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x62, 0x96, 0x00, 0x00, 0xB7, 0x89,
			0x00, 0x00, 0x18, 0xDA, 0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x24, 0xA0, 0x00, 0x00, 0x0F, 0x85, 0x00, 0x00, 0xB6, 0xC4,
			0x63, 0x75, 0x72, 0x76, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14,
			0x00, 0x00, 0x01, 0x07, 0x02, 0xB5, 0x05, 0x6B, 0x09, 0x36, 0x0E, 0x50,
			0x14, 0xB1, 0x1C, 0x80, 0x25, 0xC8, 0x30, 0xA1, 0x3D, 0x19, 0x4B, 0x40,
			0x5B, 0x27, 0x6C, 0xDB, 0x80, 0x6B, 0x95, 0xE3, 0xAD, 0x50, 0xC6, 0xC2,
			0xE2, 0x31, 0xFF, 0xFF, 0x00, 0x70, 0x64, 0x76
		},
			// The above vector "ProfileDataVec", containing the basic profile, that will later be compressed (deflate), 
			// and will also contain the encrypted & compressed user's data file, will itself be inserted into this vector "ProfileChunkVec". 
			// The vector initially contains just the PNG iCCP Profile chunk header (chunk length field, name and crc value field).

			pdv.ProfileChunkVec = {
			0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x49, 0x43, 0x43, 0x20,
			0x50, 0x72, 0x6F, 0x66, 0x69, 0x6C, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00 };

		// Read-in user's data file and store it in vector "FileVec".
		pdv.FileVec.assign(std::istreambuf_iterator<char>(readFile), std::istreambuf_iterator<char>());
		
		pdv.EncryptedVec.reserve(pdv.DATA_SIZE);

		const std::string
			PNG_SIG = "\x89PNG",
			PNG_CHECK{ pdv.ImageVec.begin(), pdv.ImageVec.begin() + PNG_SIG.length() };	// Get image header from vector. 

		// Make sure image has valid PNG header.
		if (PNG_CHECK != PNG_SIG) {
			// File requirements check failure, display relevant error message and exit program.
			std::cerr << "\nImage Error: File does not appear to be a valid PNG image.\n\n";
			std::exit(EXIT_FAILURE);
		}

		// Find and remove unwanted PNG chunks.
		eraseChunks(pdv.ImageVec, pdv);
	}
}

void encryptDecrypt(pdvStruct& pdv) {

		if (pdv.MODE == "-i") {

			// We don't want "./" or ".\" characters at the start of the filename (user's data file), which we will store in the profile. 
			std::size_t lastSlashPos = pdv.DATA_FILE.find_last_of("\\/");

			if (lastSlashPos <= pdv.DATA_FILE.length()) {
				std::string_view new_name(pdv.DATA_FILE.c_str() + (lastSlashPos + 1), pdv.DATA_FILE.length() - (lastSlashPos + 1));
				pdv.DATA_FILE = new_name;
			}
		}

		const std::string XOR_KEY = "\xFC\xD8\xF9\xE2\x9F\x7E";	// String used to XOR encrypt/decrypt the filename of user's data file.

		size_t indexPos = 0;

		const int MAX_LENGTH_FILENAME = 23;

		const size_t
			NAME_LENGTH = pdv.DATA_FILE.length(),	// Filename length of user's data file.
			XOR_KEY_LENGTH = XOR_KEY.length();

		// Make sure character length of filename does not exceed set maximum.
		if (NAME_LENGTH > MAX_LENGTH_FILENAME) {
			std::cerr << "\nFile Error: Filename length of your data file (" + std::to_string(NAME_LENGTH) + " characters) is too long.\n"
				"\nFor compatibility requirements, your filename must be under 24 characters.\nPlease try again with a shorter filename.\n\n";
			std::exit(EXIT_FAILURE);
		}

		std::string
			inName = pdv.DATA_FILE,
			outName;

		SBYTE
			xorKeyStartPos = 0,
			xorKeyPos = xorKeyStartPos,	// Character position variable for XOR_KEY string.
			nameKeyStartPos = 0,
			nameKeyPos = nameKeyStartPos;	// Character position variable for filename string (outName / inName).

		if (NAME_LENGTH > pdv.DATA_SIZE) {
			std::cerr << "\nFile Size Error: File size is too small.\n\n";
			std::exit(EXIT_FAILURE);
		}

		// XOR encrypt or decrypt filename and data file.
		while (pdv.DATA_SIZE > indexPos) {

			if (indexPos >= NAME_LENGTH) {
				nameKeyPos = nameKeyPos > NAME_LENGTH ? nameKeyStartPos : nameKeyPos;	 // Reset filename character position to the start if it's reached last character.
			}
			else {
				xorKeyPos = xorKeyPos > XOR_KEY_LENGTH ? xorKeyStartPos : xorKeyPos;	// Reset XOR_KEY position to the start if it has reached last character.
				outName += inName[indexPos] ^ XOR_KEY[xorKeyPos++];	// XOR each character of filename against characters of XOR_KEY string. Store output characters in "outName".
											// Depending on Mode, filename is either encrypted or decrypted.
			}

			if (pdv.MODE == "-i") {
				// Encrypt data file. XOR each byte of the data file within vector "FileVec" against each character of the encrypted filename, "outName". 
				// Store encrypted output in vector "ProfileDataVec".
				pdv.ProfileDataVec.emplace_back(pdv.FileVec[indexPos++] ^ outName[nameKeyPos++]);
			}
			else {
				// Decrypt data file: XOR each byte of the data file within vector "ImageVec" against each character of the encrypted filename, "inName". 
				// Store decrypted output in vector "DecryptedVec".
				pdv.DecryptedVec.emplace_back(pdv.ImageVec[indexPos++] ^ inName[nameKeyPos++]);
			}
		}

		if (pdv.MODE == "-i") {

			SBYTE
				profileDataVecSizeIndex = 0,
				profileChunkSizeIndex = 0,	// Start index of the PNG iCCP Profile header chunk's 4 byte length field.
				profileNameLengthIndex = 100,	// Index location inside the profile to store the length value of the user's data filename (Max. 1 byte).
				profileNameIndex = 101,		// Start index location inside the profile to store the filename for the user's embedded data file.
				bits = 32;			// 1 byte. Value used in "updateValue" function.
		
			// Insert the character length value of the filename into the profile,
			// We need to know how many characters to read when we later retrieve the filename from the profile, during file extraction.
			pdv.ProfileDataVec[profileNameLengthIndex] = static_cast<int>(NAME_LENGTH);

			// Make space for the filename by removing equivalent length of characters from profile.
			pdv.ProfileDataVec.erase(pdv.ProfileDataVec.begin() + profileNameIndex, pdv.ProfileDataVec.begin() + outName.length() + profileNameIndex);

			// Insert the encrypted filename into the profile.
			pdv.ProfileDataVec.insert(pdv.ProfileDataVec.begin() + profileNameIndex, outName.begin(), outName.end());

			// Update internal Profile Size 
			update->Value(pdv.ProfileDataVec, profileDataVecSizeIndex, pdv.ProfileDataVec.size(), bits);

			bool inflate = false; // deflate (compress).
			
			// Call function to deflate/compress the contents of vector "ProfileDataVec" (Basic profile with user's (encrypted) data file).
			inflateDeflate(pdv.ProfileDataVec, inflate);
		
			const size_t
				PROFILE_DEFLATE_SIZE = pdv.ProfileDataVec.size(),
				PROFILE_CHUNK_SIZE = PROFILE_DEFLATE_SIZE + 17, // "iCCPICC Profile\x00\x00" (17 chars).
				PROFILE_DATA_INSERT_INDEX = 21,   // Index location within ProfileChunkVec to insert the deflate contents of ProfileDataVec. 		   
				PROFILE_CHUNK_INSERT_INDEX = 33;  // Index location within ImageVec (image file) to insert ProfileChunkVec (iCCP Profile chunk).

			// Insert the deflate/compressed iCCP Profile (with user's encrypted/compressed data file) into vector "ProfileChunkVec", begining at index location 21.
			pdv.ProfileChunkVec.insert((pdv.ProfileChunkVec.begin() + PROFILE_DATA_INSERT_INDEX), pdv.ProfileDataVec.begin(), pdv.ProfileDataVec.end());

			// Call function to insert new chunk length value into "ProfileChunkVec" vector's index length field. 
			update->Value(pdv.ProfileChunkVec, profileChunkSizeIndex, PROFILE_DEFLATE_SIZE + 13, bits);

			const int PROFILE_CHUNK_START_INDEX = 4;

			// Pass these two values (PROFILE_CHUNK_START_INDEX and PROFILE_CHUNK_SIZE) to the CRC fuction to get correct iCCP Profile chunk CRC.
			const size_t PROFILE_CHUNK_CRC = crc(&pdv.ProfileChunkVec[PROFILE_CHUNK_START_INDEX], PROFILE_CHUNK_SIZE);

			// Index location for the 4-byte CRC field of the iCCP Profile chunk.
			size_t profileCrcInsertIndex = PROFILE_CHUNK_START_INDEX + (PROFILE_CHUNK_SIZE);

			// Call function to insert iCCP Profile chunk CRC value into "ProfileChunkVec" vector's CRC index field. 
			update->Value(pdv.ProfileChunkVec, profileCrcInsertIndex, PROFILE_CHUNK_CRC, bits);
			
			// Insert contents of vector "ProfileChunkVec" into vector "ImageVec", combining iCCP Profile chunk (compressed profile & user's data file) with PNG image.
			pdv.ImageVec.insert((pdv.ImageVec.begin() + PROFILE_CHUNK_INSERT_INDEX), pdv.ProfileChunkVec.begin(), pdv.ProfileChunkVec.end());

			// If we embed multiple data files (Max. 6), each outputted image will be differentiated by a number in the filename, e.g. jdv_img1.jpg, jdv_img2.jpg, jdv_img3.jpg.
			std::string diffVal = std::to_string(pdv.subVal - pdv.imgVal);	
			pdv.DATA_FILE = "pdv_img" + diffVal + ".png";

			// Write out to file the PNG image with the embedded (encrypted/compressed) user's data file.
			writeOutFile(pdv);
		}
		else { // Extract Mode.

			// Update string variable with the decrypted filename.
			pdv.DATA_FILE = outName;

			// Write the extracted (inflated / decrypted) data file out to disk.
			writeOutFile(pdv);
		}
	}

void inflateDeflate(std::vector<BYTE>& Vec, bool inflateData) {

	// zlib function, see https://zlib.net/

	std::vector <BYTE> Buffer;

	size_t BUFSIZE;

	if (!inflateData) {
		BUFSIZE = 1032 * 1024;  // Required for deflate. This BUFSIZE covers us to our Max. file size. A lower BUFSIZE results in lost data.
	}
	else {
		BUFSIZE = 256 * 1024;  // Fine for inflate.
	}

	BYTE* temp_buffer{ new BYTE[BUFSIZE] };

	z_stream strm;
	strm.zalloc = 0;
	strm.zfree = 0;
	strm.next_in = Vec.data();
	strm.avail_in = static_cast<uInt>(Vec.size());
	strm.next_out = temp_buffer;
	strm.avail_out = static_cast<uInt>(BUFSIZE);

	if (inflateData) {
		inflateInit(&strm);
	}
	else {
		deflateInit(&strm, 6); // Compression level 6 (78, 9c...)
	}

	while (strm.avail_in)
	{
		if (inflateData) {
			inflate(&strm, Z_NO_FLUSH);
		}
		else {
			deflate(&strm, Z_NO_FLUSH);
		}

		if (!strm.avail_out)
		{
			Buffer.insert(Buffer.end(), temp_buffer, temp_buffer + BUFSIZE);
			strm.next_out = temp_buffer;
			strm.avail_out = static_cast<uInt>(BUFSIZE);
		}
		else
			break;
	}
	if (inflateData) {
		inflate(&strm, Z_FINISH);
		Buffer.insert(Buffer.end(), temp_buffer, temp_buffer + BUFSIZE - strm.avail_out);
		inflateEnd(&strm);
	}
	else {
		deflate(&strm, Z_FINISH);
		Buffer.insert(Buffer.end(), temp_buffer, temp_buffer + BUFSIZE - strm.avail_out);
		deflateEnd(&strm);
	}

	Vec.swap(Buffer);
	delete[] temp_buffer;
}

void eraseChunks(std::vector<BYTE>& Vec, pdvStruct& pdv) {

	std::string CHUNK[15]{ "IDAT", "bKGD", "cHRM", "sRGB", "hIST", "iCCP", "pHYs", "sBIT", "gAMA", "sPLT", "tIME", "tRNS", "tEXt", "iTXt", "zTXt" };

	if (pdv.MODE == "-x") {  // If extracting data file from the image, we need to keep the iCCP chunk.
		CHUNK[5] = "KEEP_ME";
	}

	for (int chunkIndex = 14; chunkIndex > 0; chunkIndex--) {
		size_t
			// Get first IDAT chunk index location. Don't remove chunks after this location.
			firstIdatIndex = search(Vec.begin(), Vec.end(), CHUNK[0].begin(), CHUNK[0].end()) - Vec.begin(),
			// From last to first, search and get index location of each chunk to remove.
			chunkFoundIndex = search(Vec.begin(), Vec.end(), CHUNK[chunkIndex].begin(), CHUNK[chunkIndex].end()) - Vec.begin() - 4;
		// If found chunk is located before first IDAT, remove it.
		if (firstIdatIndex > chunkFoundIndex) {
			int chunkSize = (Vec[chunkFoundIndex + 1] << 16) | Vec[chunkFoundIndex + 2] << 8 | Vec[chunkFoundIndex + 3];
			Vec.erase(Vec.begin() + chunkFoundIndex, Vec.begin() + chunkFoundIndex + (chunkSize + 12));
			chunkIndex++; // Increment chunkIndex so that we search again for the same chunk, in case of multiple occurrences.
		}
	}
	// Now that we have removed unwanted chunks, encrypt the user's data file (if embedding file).
	if (pdv.MODE == "-i") {
		encryptDecrypt(pdv);
	}
}

size_t updateCrc(const size_t& crc, BYTE* buf, const size_t& len)
{

	size_t crcTable[256]{ 0,1996959894,3993919788,2567524794,124634137,1886057615,3915621685,2657392035,249268274,2044508324,3772115230,2547177864,162941995,2125561021,3887607047,
						2428444049,498536548,1789927666,4089016648,2227061214,450548861,1843258603,4107580753,2211677639,325883990, 1684777152,4251122042,2321926636,335633487,1661365465,
						4195302755,2366115317,997073096,1281953886,3579855332,2724688242,1006888145,1258607687,3524101629,2768942443,901097722,1119000684,3686517206,2898065728,853044451,
						1172266101,3705015759,2882616665,651767980, 1373503546,3369554304,3218104598,565507253,1454621731,3485111705,3099436303,671266974,1594198024,3322730930,2970347812,
						795835527,1483230225,3244367275,3060149565,1994146192,31158534,2563907772,4023717930,1907459465,112637215,2680153253,3904427059,2013776290,251722036,2517215374,
						3775830040,2137656763,141376813,2439277719,3865271297,1802195444,476864866,2238001368,4066508878,1812370925,453092731,2181625025,4111451223,1706088902,314042704,
						2344532202,4240017532,1658658271,366619977,2362670323,4224994405,1303535960,984961486,2747007092,3569037538,1256170817,1037604311,2765210733,3554079995,1131014506,
						879679996,2909243462,3663771856,1141124467,855842277,2852801631,3708648649,1342533948,654459306,3188396048,3373015174,1466479909,544179635,3110523913,3462522015,
						1591671054,702138776,2966460450,3352799412,1504918807,783551873,3082640443,3233442989,3988292384,2596254646,62317068,1957810842,3939845945,2647816111,81470997,
						1943803523,3814918930,2489596804,225274430,2053790376,3826175755,2466906013,167816743,2097651377,4027552580,2265490386,503444072,1762050814,4150417245,2154129355,
						426522225,1852507879,4275313526,2312317920,282753626,1742555852,4189708143,2394877945,397917763,1622183637,3604390888,2714866558,953729732,1340076626,3518719985,
						2797360999,1068828381,1219638859,3624741850,2936675148,906185462,1090812512,3747672003,2825379669,829329135,1181335161,3412177804,3160834842,628085408,1382605366,
						3423369109,3138078467,570562233,1426400815,3317316542,2998733608,733239954,1555261956,3268935591,3050360625,752459403,1541320221,2607071920,3965973030,1969922972,
						40735498,2617837225,3943577151,1913087877,83908371,2512341634,3803740692,2075208622,213261112,2463272603,3855990285,2094854071,198958881,2262029012,4057260610,
						1759359992,534414190,2176718541,4139329115,1873836001,414664567,2282248934,4279200368,1711684554,285281116,2405801727,4167216745,1634467795,376229701,2685067896,
						3608007406,1308918612,956543938,2808555105,3495958263,1231636301,1047427035,2932959818,3654703836,1088359270,936918000,2847714899,3736837829,1202900863,817233897,
						3183342108,3401237130,1404277552,615818150,3134207493,3453421203,1423857449,601450431,3009837614,3294710456,1567103746,711928724,3020668471,3272380065,1510334235,
						755167117 };

	size_t c = crc;

	for (size_t n{}; n < len; n++) {
		c = crcTable[(c ^ buf[n]) & 0xff] ^ (c >> 8);
	}
	return c;
}

size_t crc(BYTE* buf, const size_t& len)
{
	return updateCrc(0xffffffffL, buf, len) ^ 0xffffffffL;
}

void writeOutFile(pdvStruct& pdv) {

	std::ofstream writeFile(pdv.DATA_FILE, std::ios::binary);

	if (!writeFile) {
		std::cerr << "\nWrite Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}

	if (pdv.MODE == "-i") {

		// Write out to disk the PNG image embedded with the user's encrypted/compressed data file.
		writeFile.write((char*)&pdv.ImageVec[0], pdv.ImageVec.size());

		std::cout << "\nCreated output file: \"" + pdv.DATA_FILE + " " << pdv.ImageVec.size() << " " << "Bytes\"\n";
		
		pdv.EncryptedVec.clear();
	}
	else {
		// Write out to disk the extracted data file.
		writeFile.write((char*)&pdv.DecryptedVec[0], pdv.DecryptedVec.size());
		std::cout << "\nExtracted file: \"" + pdv.DATA_FILE + " " << pdv.DecryptedVec.size() << " " << "Bytes\"\n";
		pdv.DecryptedVec.clear();
	}
}

void displayInfo() {

	std::cout << R"(
PNG Data Vehicle for Mastodon & Reddit, (pdvrdt v1.4). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

This command line tool enables you to embed & extract any file type of upto *16MB within a PNG image.
You can then upload & share your embedded image file on Mastodon, 16MB max. or *Reddit 1MB max. (Desktop only. Reddit mobile app not supported).

If your data file is under 10KB, you can also post your "file-embedded" image to Twitter. 

Other image hosting sites may also be compatible with pdvrdt embedded images.

To embed larger files for Twitter (5MB max.), please use pdvzip, (PNG images).
To embed larger files for Reddit (20MB max.), please use jdvrif (JPG images). You can also use jdvrif for Mastodon (16MB max.).

This program works on Linux and Windows.

For Mastodon, the 16MB size limit is measured by the total size of your "file-embedded" PNG image file. 
For Reddit and Twitter, the size limit is measured by the uncompressed size of the iCC Profile, where your data is stored.

Reddit: 1,048,172 bytes is the uncompressed (zlib inflate) size limit for your data file.
404 bytes are used for the basic iCC Profile. (404 + 1048172 = 1,048,576 bytes [1MB]).

Twitter: 9,836 bytes is the uncompressed (zlib inflate) limit for your data file.
404 bytes are used for the basic iCC Profile (404 + 9836 = 10,240 bytes [10KB])

To maximise the amount of data you can embed in your image file for Reddit & Twitter, compress the data file to a ZIP or RAR file, etc. 
Make sure the compressed ZIP or RAR file does not exceed 1,048,172 bytes for Reddit or 9,836 bytes for Twitter. 

You can insert up to six files at a time (outputs one image per file).
You can also extract files from up to six images at a time.

)";
}
