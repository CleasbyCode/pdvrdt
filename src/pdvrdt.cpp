//	PNG Data Vehicle for Reddit, (PDVRDT v1.2). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <zlib.h>

// Open user image & data file and check file size requirements. Display error & exit program if any file fails to open or exceeds size limits.
void processFiles(char* []);

// Open pdvrdt PNG image file, then proceed to inflate/uncompress, decrypt and extract embedded data file from it. 
void processEmbeddedImage(char* []);

// Read in and store PNG image & data file into vectors. Data file is encrypted and deflate/compressed (zlib).
void readFilesIntoVectors(std::ifstream&, std::ifstream&, const std::string&, const std::string&, const ptrdiff_t&, const ptrdiff_t&);

// Search and remove all unnecessary PNG chunks found before the first IDAT chunk.
void eraseChunks(std::vector<unsigned char>&);

// Write content of various vectors out to file.
void writeFile(std::vector<unsigned char>&, const std::string&);

// Insert updated values, such as chunk size or chunk crc, into relevant vector index locations.
void insertValue(std::vector<unsigned char>&, ptrdiff_t, const size_t&, int);

// Inflate or Deflate iCCP Profile chunk, which include user's data file.
void inflateDeflate(std::vector<unsigned char>&, bool);

// Display program infomation
void displayInfo();

// Code to compute CRC32 (for iCCP Profile chunk within this program) was taken from: https://www.w3.org/TR/2003/REC-PNG-20031110/#D-CRCAppendix 
unsigned long crcUpdate(const unsigned long&, unsigned char*, const size_t&);
unsigned long crc(unsigned char*, const size_t&);

const std::string READ_ERR_MSG = "\nRead Error: Unable to open/read file: ";

int main(int argc, char** argv) {

	if (argc == 2 && std::string(argv[1]) == "--info") {
		argc = 0;
		displayInfo();
	}
	else if (argc < 2 || argc > 6) {
		std::cerr << "\nUsage:-\n\tInsert:  pdvrdt  <png_image>  <your_file>\n\tExtract: pdvrdt  <png_image> ... ... ... ...\n\tHelp:\t pdvrdt  --info\n\n";
		argc = 0;
	}
	else if (argc == 3) {
		
		std::string
			secondFile = argv[2],
			response;
		
		std::transform(secondFile.begin(), secondFile.end(), secondFile.begin(),
			[](unsigned char c) { return std::tolower(c); });

		if (secondFile.length() > 2 && secondFile.substr(secondFile.length() - 3, secondFile.length()) == "png") {
			std::cout << "\nClarification required.\n\nPlease type YES if you intend to embed the file: \""
						<< secondFile << "\"\notherwise just press enter to continue with extraction: ";
			std::getline(std::cin, response);
			if (response == "YES") {
				processFiles(argv);
			}
			else {
				processEmbeddedImage(argv++);
				processEmbeddedImage(argv);
			}
		}
		else {
			while (argc !=2){
			processFiles(argv++);
			argc--;
			}
		}
	}
	else {
		while (argc !=1 ) {
			processEmbeddedImage(argv++);
			argc--;
			}
	}
	if (argc !=0) std::cout << "All Done!\n\n";
	return 0;
}

void processFiles(char* argv[]) {

	const std::string
		IMAGE_FILE = argv[1],
		DATA_FILE = argv[2];

	std::ifstream
		readImage(IMAGE_FILE, std::ios::binary),
		readFile(DATA_FILE, std::ios::binary);

	if (!readImage || !readFile) {

		// Open file failure, display relevant error message and exit program.
		const std::string ERR_MSG = !readImage ? READ_ERR_MSG + "\"" + IMAGE_FILE + "\"\n\n" : READ_ERR_MSG + "\"" + DATA_FILE + "\"\n\n";

		std::cerr << ERR_MSG;
		std::exit(EXIT_FAILURE);
	}

	// Open file success, now check file size requirements.
	const int
		MAX_PNG_SIZE_BYTES = 5242880,		// Reddit PNG size limit 5MB.
		MAX_DATAFILE_SIZE_BYTES = 1048444;	// 1MB (1,048,576 bytes maximum zlib uncompressed iCCP Profile size),
							// minus 132 bytes for the required barebones profile, gives us 1,048,444 bytes (uncompressed) for data file.
	// Get size of files.
	readImage.seekg(0, readImage.end),
	readFile.seekg(0, readFile.end);

	const ptrdiff_t
		IMAGE_SIZE = readImage.tellg(),
		DATA_SIZE = readFile.tellg();

	if ((IMAGE_SIZE + MAX_DATAFILE_SIZE_BYTES) > MAX_PNG_SIZE_BYTES
		|| DATA_SIZE > MAX_DATAFILE_SIZE_BYTES) {

		// File size check failure, display relevant error message and exit program.
		const std::string
			SIZE_ERR_PNG = "\nImage Size Error: PNG image (+including embedded file size) must not exceed 5MB.\n\n",
			SIZE_ERR_DATA = "\nFile Size Error: Your data file must not exceed 1,048,444 bytes.\n\n",

			ERR_MSG = IMAGE_SIZE + MAX_DATAFILE_SIZE_BYTES > MAX_PNG_SIZE_BYTES ? SIZE_ERR_PNG : SIZE_ERR_DATA;

		std::cerr << ERR_MSG;
		std::exit(EXIT_FAILURE);
	}

	// File size check success, now read-in and store files into vectors.
	readFilesIntoVectors(readImage, readFile, IMAGE_FILE, DATA_FILE, IMAGE_SIZE, DATA_SIZE);
}

// Inflate, decrypt and extract embedded data file from PNG image.
void processEmbeddedImage(char* argv[]) {

	const std::string IMAGE_FILE = argv[1];

	std::ifstream readImage(IMAGE_FILE, std::ios::binary);

	if (!readImage) {
		std::cerr << READ_ERR_MSG + "\"" +IMAGE_FILE + "\"\n\n";
		std::exit(EXIT_FAILURE);
	}

	// Read pdvrdt file embedded PNG image into vector "ImageVec".
	std::vector<unsigned char> ImageVec((std::istreambuf_iterator<char>(readImage)), std::istreambuf_iterator<char>());

	// When reddit encodes the uploaded PNG image, it will write the PLTE chunk (if image is PNG-8) before the iCCP Profile chunk. We need to check for this.
	int
		plteChunkSize = 0,	// If no PLTE chunk, then this value remains 0.
		plteChunkSizeIndex = 33,
		chunkIndex = 37;	// If PNG-8, chunk index location of PLTE chunk, else iCCP Profile chunk index location. 

	const std::string 
		PLTE_SIG = "PLTE",
		PLTE_CHECK{ ImageVec.begin() + chunkIndex, ImageVec.begin() + chunkIndex + PLTE_SIG.length() };

	if (PLTE_CHECK == PLTE_SIG) {
		// Get length of the PLTE chunk
		plteChunkSize = 12 + (ImageVec[plteChunkSizeIndex + 1] << 16) | ImageVec[plteChunkSizeIndex + 2] << 8 | ImageVec[plteChunkSizeIndex + 3];
	}

	// Get iCCP Profile chunk signature from vector.
	const std::string
		PROFILE_SIG = "iCCPICC\x20Profile",
		PROFILE_CHECK{ ImageVec.begin() + chunkIndex + plteChunkSize, ImageVec.begin() + chunkIndex + plteChunkSize + PROFILE_SIG.length() };

	if (PROFILE_CHECK != PROFILE_SIG) {

		// File requirements check failure, display relevant error message and exit program.
		std::cerr << "\nPNG Error: Image file \"" << IMAGE_FILE << "\" does not appear to contain a valid iCCP profile.\n\n";
		std::exit(EXIT_FAILURE);
	}
	
	std::vector<unsigned char> IhdrVec{};

	int 
		ihdrIndex = 8,
		ihdrLength = 25;

	// Before we erase most of the "ImageVec" vector, save the IHDR chunk into this vector. 
	IhdrVec.insert(IhdrVec.begin(), ImageVec.begin() + ihdrIndex, ImageVec.begin() + ihdrIndex + ihdrLength);
	
	int
		profileChunkSizeIndex = 33 + plteChunkSize, // Start index location of 4 byte iCCP Profile chunk length field.
		// Get iCCP Profile chunk length.
		profileChunkSize = (ImageVec[profileChunkSizeIndex + 1] << 16) | ImageVec[profileChunkSizeIndex + 2] << 8 | ImageVec[profileChunkSizeIndex + 3],
		deflateDataIndex = 54 + plteChunkSize, // Start index location of deflate/compressed (zlib) data within iCCP Profile chunk (78,9C...)
		deflateChunkSize = profileChunkSize - 13;

	// From "ImageVec" vector index 0, erase bytes so that start of vector is now the beginning of the deflate/compressed (zlib) data.
	ImageVec.erase(ImageVec.begin(), ImageVec.begin() + deflateDataIndex);

	// Erase all bytes of "ImageVec" after end of deflate/compressed data. Vector should now just contain the zlib deflate/compressed data.
	ImageVec.erase(ImageVec.begin() + deflateChunkSize, ImageVec.end());
	
	bool inflateData = true;
	
	// Call function to inflate/uncompress profile chunk, which includes user's data file. 
	inflateDeflate(ImageVec, inflateData);
	
	const std::string
		PDV_SIG = "PDVRDT",
		PDV_CHECK{ ImageVec.begin() + 5, ImageVec.begin() + 5 + PDV_SIG.length() },		// Get PDVRDT signature from vector "ImageVec".
		ENCRYPTED_NAME = { ImageVec.begin() + 13, ImageVec.begin() + 13 + ImageVec[12] };	// Get encrypted embedded filename from vector "ImageVec".

	int encryptedNameLength = ImageVec[12];

	// Make sure this is a pdvrdt file-embedded image.
	if (PDV_CHECK != PDV_SIG) {
		// File requirements check failure, display relevant error message and exit program.
		std::cerr << "\nProfile Error: iCCP profile does not seem to be a PDVRDT modified profile.\n\n";
		std::exit(EXIT_FAILURE);
	}

	// Check to see if reddit has altered the IHDR chunk of the downloaded image.
	// reddit may change the bit-depth and/or interlace value, which would also change the IHDR crc.
	// We need the original (before posted to reddit) IHDR crc to decrypt the embedded filename.

	bool updateIhdrCrc = true;

	if (IhdrVec[16] != ImageVec[4] && IhdrVec[20] != ImageVec[11]) {
		// Both bit-depth and interlace values changed. Restore the original values stored in TmpProfileVec.
		IhdrVec[16] = ImageVec[4];
		IhdrVec[20] = ImageVec[11];
	}
	else if (IhdrVec[16] != ImageVec[4]) {
		// Just the bit-depth changed. Restore.
		IhdrVec[16] = ImageVec[4];
	}
	else if (IhdrVec[20] != ImageVec[11]) {
		// Just the interlace value changed. Restore
		IhdrVec[20] = ImageVec[11];
	}
	else updateIhdrCrc = false; // No change to image, no need to restore IHDR crc.

	if (updateIhdrCrc) {
		// IHDR chunk was modified by reddit, restore the crc.
		const int
			IHDR_CHUNK_START_INDEX = 4,
			IHDR_CHUNK_SIZE = 17;

		// Now update/restore IHDR crc after reseting original values changed by reddit.
		const uint32_t IHDR_CHUNK_CRC = crc(&IhdrVec[IHDR_CHUNK_START_INDEX], IHDR_CHUNK_SIZE);

		int ihdrCrcInsertIndex = 21;

		insertValue(IhdrVec, ihdrCrcInsertIndex, IHDR_CHUNK_CRC, 32);
	}

	// Erase first 132 bytes from "ImageVec", which removes the barebones iCCP Profile data. Only the embedded data file remains.
	ImageVec.erase(ImageVec.begin(), ImageVec.begin() + 132);
	
	// Store the decrypted embedded file in this vector.
	std::vector<unsigned char> ExtractedFileVec;

	std::string decryptedName;

	bool nameDecrypted = false;

	// Decrypt the embedded filename and the embedded data file.
	for (int i = 0, charPos = 0, ihdrCrcIndex = 21 ; ImageVec.size() > i; i++) {
		if (!nameDecrypted) {
			decryptedName += ENCRYPTED_NAME[charPos] ^ IhdrVec[ihdrCrcIndex++];
			ihdrCrcIndex = ihdrCrcIndex > 24 ? 21 : ihdrCrcIndex;
		}
		ExtractedFileVec.insert(ExtractedFileVec.begin() + i, ImageVec[i] ^ ENCRYPTED_NAME[charPos++]);
		if (charPos >= encryptedNameLength) {
			nameDecrypted = true;
			charPos = charPos > encryptedNameLength ? 0 : charPos;
		}
	}
	
	if (decryptedName.substr(0,7) != "pdvrdt_") {
		decryptedName = "pdvrdt_" + decryptedName;
	}
	
	// Write extracted data out to file.
	writeFile(ExtractedFileVec, decryptedName);

	std::cout << "\nCreated output file: \"" + decryptedName + "\"\n\n";

	readImage.close();
}

void readFilesIntoVectors(std::ifstream& readImage, std::ifstream& readFile, const std::string& IMAGE_FILE, const std::string& DATA_FILE, const ptrdiff_t& IMAGE_SIZE, const ptrdiff_t& DATA_SIZE) {

	// Reset position of files. 
	readImage.seekg(0, readImage.beg),
	readFile.seekg(0, readFile.beg);

	// User's data file is first inserted and stored at the end of this inflate/uncompressed iCCP Profile.
	// The first 132 bytes of this vector contains the barebones profile (inflate/uncompressed).
	// Without this basic profile, some image display programs will show error messages when loading the image.
	std::vector<unsigned char>
		ProfileDataVec{
			0x00, 0x00, 0x00, 0x84, 0x20, 0x50, 0x44, 0x56, 0x52, 0x44, 0x54, 0x20,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x61, 0x63, 0x73, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	},
		// The deflate/compressed (zlib) iCCP Profile (+ user's data file) will be inserted in the "ProfileChunkVec" vector, within the PNG iCCP Profile chunk.
		ProfileChunkVec
	{
			0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x49, 0x43, 0x43, 0x20,
			0x50, 0x72, 0x6F, 0x66, 0x69, 0x6C, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00 },

		// Read-in user PNG image file and store in vector "ImageVec".
		ImageVec((std::istreambuf_iterator<char>(readImage)), std::istreambuf_iterator<char>());

	srand((unsigned)time(NULL));

	const std::string 
		TXT_NUM = std::to_string(rand()),
		EMBEDDED_IMAGE_FILE = "pdvrdt_image_"+TXT_NUM.substr(0,6)+".png",
		PNG_SIG = "\x89PNG",
		PNG_CHECK{ ImageVec.begin(), ImageVec.begin() + PNG_SIG.length() };	// Get image header from vector. 
											
	// Make sure image has valid PNG header.
	if (PNG_CHECK != PNG_SIG) {
		// File requirements check failure, display relevant error message and exit program.
		std::cerr << "\nImage Error: File does not appear to be a valid PNG image.\n";
		std::exit(EXIT_FAILURE);
	}

	// Find and remove unwanted chunks.
	eraseChunks(ImageVec);

	const int MAX_LENGTH_FILENAME = 23;

	// We don't want "./" or ".\" characters at the start of the filename (user's data file), that we will store in the iCCP Profile. 
	std::size_t firstSlashPos = DATA_FILE.find_first_of("\\/");
	std::string 
		noSlashName = DATA_FILE.substr(firstSlashPos + 1, DATA_FILE.length()),
		encryptedName;
	
	ptrdiff_t
		profileChunkSizeIndex = 1,	// Start index of the PNG iCCP Profile chunk's 4 byte length field (only 3 bytes used).
		profileInflateSizeIndex = 1,	// Start index location inside iCCP profile of its inflate size field (again, only 3 bytes out of the four, used).
		profileNameLengthIndex = 12,	// Index location inside the iCCP Profile to store the length value of the embedded data's filename (1 byte).
		profileNameIndex = 13,		// Start index inside the iCCP Profile to store the filename for the embedded data file.
		noSlashNameLength = noSlashName.length(); // Character length of filename for the embedded data file.

	// Make sure character length of filename (user's data file) does not exceed set maximum.
	if (noSlashNameLength > MAX_LENGTH_FILENAME) {
		std::cerr << "\nFile Error: Filename length of your data file (" + std::to_string(noSlashNameLength) + " characters) is too long.\n"
				"\nFor compatibility requirements, your filename must be under 24 characters.\nPlease try again with a shorter filename.\n\n";
		std::exit(EXIT_FAILURE);
	}

	// Store the bit-depth and interlace method values in the profile. 
	// We need to check these values against the downloaded reddit image, to see if reddit has changed them, 
	// which would also mean a changed IHDR crc value. 
	ProfileDataVec[4] = ImageVec[24];	// Bit-depth value from ImageVec stored in ProfileDataVec
	ProfileDataVec[11] = ImageVec[28];	// Interlace method value from ImageVec stored in ProfileDataVec

	// Insert the character length value of the filename (user's data file) into the iCCP Profile,
	// We need to know how many characters to read when later retrieving the filename from the profile.
	insertValue(ProfileDataVec, profileNameLengthIndex, noSlashNameLength, 8);

	// Make space for this data by removing equivalent length of characters from iCCP Profile.
	ProfileDataVec.erase(ProfileDataVec.begin() + profileNameIndex, ProfileDataVec.begin() + noSlashNameLength + profileNameIndex);

	// Encrypt filename.
	for (int i = 0, ihdrCrcIndex = 29; noSlashNameLength != 0; noSlashNameLength--) {
		encryptedName += noSlashName[i++] ^ ImageVec[ihdrCrcIndex++];	// xor each character of the filename against each byte of the 4-byte IHDR crc value.
		ihdrCrcIndex = ihdrCrcIndex > 32 ? 29 : ihdrCrcIndex;		// Reset position of IHDR crc once we are on the fourth, last byte.
	}

	// Insert the encrypted filename into the basic iCCP Profile.
	ProfileDataVec.insert(ProfileDataVec.begin() + profileNameIndex, encryptedName.begin(), encryptedName.end());

	char byte;
	
	// Read-in user's data file, appending it to the "ProfileDataVec" vector.
	// Xor encrypt each byte of the user's data file against each character of the encrypted filename.
	for (int charPos = 0, insertIndex = 132; DATA_SIZE + 132 > insertIndex ;) {
		byte = readFile.get();
		ProfileDataVec.insert((ProfileDataVec.begin() + insertIndex++), byte ^ encryptedName[charPos++]);
		charPos = charPos > encryptedName.length() ? 0 : charPos; // Reset character position of filename once we get to the last character.
	}

	// Get the size of the complete inflate/uncompressed profile (basic profile + user's data file).
	ptrdiff_t profileInflateSize = ProfileDataVec.size();
	
	// Write the inflate/uncompressed size value inside the profile within its size field (first 4-bytes of profile).
	// While it does not seem to cause any issues whether or not the internal profile size field is correctly updated, we do it regardless.
	insertValue(ProfileDataVec, profileInflateSizeIndex, profileInflateSize, 24);

	bool inflateData = false;

	// Call function for deflate/compress the contents of vector "ProfileDataVec" (Basic profile with user's (encrypted) data file).
	inflateDeflate(ProfileDataVec, inflateData);

	const ptrdiff_t
		PROFILE_DEFLATE_SIZE = ProfileDataVec.size(),
		PROFILE_CHUNK_SIZE = PROFILE_DEFLATE_SIZE + 17,
		PROFILE_DATA_INSERT_INDEX = 21,   // Index location of ProfileChunkVec for where we will insert the deflate contents of ProfileDataVec. 		   
		PROFILE_CHUNK_INSERT_INDEX = 33;  // Index location of image file ("ImageVec") for where we will insert the iCCP Profile chunk.
		
	// Insert the deflate/compressed (zlib) iCCP Profile (with user's data file) into vector "ProfileChunkVec", begining at index location 21.
	ProfileChunkVec.insert((ProfileChunkVec.begin() + PROFILE_DATA_INSERT_INDEX), ProfileDataVec.begin(), ProfileDataVec.end()); 

	// Call function to insert new chunk length value into "ProfileChunkVec" vector's index length field. 
	// Due to size limits, only 3 bytes maximum (bits = 24) of the 4 byte length field will be used.
	insertValue(ProfileChunkVec, profileChunkSizeIndex, PROFILE_DEFLATE_SIZE + 13, 24);

	const int PROFILE_CHUNK_START_INDEX = 4;

	// Pass these two values (PROFILE_CHUNK_START_INDEX and PROFILE_CHUNK_SIZE) to the CRC fuction to get correct iCCP Profile chunk CRC.
	const uint32_t PROFILE_CHUNK_CRC = crc(&ProfileChunkVec[PROFILE_CHUNK_START_INDEX], PROFILE_CHUNK_SIZE);

	// Index location for the 4-byte CRC field of the iCCP Profile chunk.
	ptrdiff_t profileCrcInsertIndex = PROFILE_CHUNK_START_INDEX + (PROFILE_CHUNK_SIZE); 

	// Call function to insert iCCP Profile chunk CRC value into "ProfileChunkVec" vector's CRC index field. 
	// Four bytes (bits = 32) will be used for the CRC value.
	insertValue(ProfileChunkVec, profileCrcInsertIndex, PROFILE_CHUNK_CRC, 32);

	// Insert contents of vector "ProfileChunkVec" into vector "ImageVec", combining iCCP Profile chunk (+ user's data file) with PNG image.
	ImageVec.insert((ImageVec.begin() + PROFILE_CHUNK_INSERT_INDEX), ProfileChunkVec.begin(), ProfileChunkVec.end());

	// Write out to file the PNG image with the embedded (compressed/encrypted) user's data file.
	writeFile(ImageVec, EMBEDDED_IMAGE_FILE);

	std::cout << "\nCreated output file: \"" + EMBEDDED_IMAGE_FILE + "\"\nYou can now post this file-embedded PNG image on reddit.\n\n";
}

void inflateDeflate(std::vector<unsigned char>& Vec, bool inflateData) {

	std::vector <unsigned char> Buffer;

	size_t BUFSIZE;
	
	if (!inflateData) {
		BUFSIZE = 1032 * 1024;  // Required for deflate. This BUFSIZE covers us to our max file size of 1MB. A lower BUFSIZE results in lost data.
	}
	else {
		BUFSIZE = 256 * 1024;  // Fine for inflate.
	}
	
	unsigned char temp_buffer[BUFSIZE];

	z_stream strm;
	strm.zalloc = 0;
	strm.zfree = 0;
	strm.next_in = Vec.data();
	strm.avail_in = Vec.size();
	strm.next_out = temp_buffer;
	strm.avail_out = BUFSIZE;

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
			strm.avail_out = BUFSIZE;
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
}

void eraseChunks(std::vector<unsigned char>& ImageVec) {

	const std::string CHUNK[15]{ "IDAT", "bKGD", "cHRM", "sRGB", "hIST", "iCCP", "pHYs", "sBIT", "gAMA", "sPLT", "tIME", "tRNS", "tEXt", "iTXt", "zTXt" };

	for (int chunkIndex = 14; chunkIndex > 0; chunkIndex--) {
		ptrdiff_t
			// Get first IDAT chunk index location. Don't remove chunks after this point.
			firstIdatIndex = search(ImageVec.begin(), ImageVec.end(), CHUNK[0].begin(), CHUNK[0].end()) - ImageVec.begin(),
			// From last to first, search and get index location of each chunk to remove.
			chunkFoundIndex = search(ImageVec.begin(), ImageVec.end(), CHUNK[chunkIndex].begin(), CHUNK[chunkIndex].end()) - ImageVec.begin() - 4;
		// If found chunk is located before first IDAT, remove it.
		if (firstIdatIndex > chunkFoundIndex) {
			int chunkSize = (ImageVec[chunkFoundIndex + 1] << 16) | ImageVec[chunkFoundIndex + 2] << 8 | ImageVec[chunkFoundIndex + 3];
			ImageVec.erase(ImageVec.begin() + chunkFoundIndex, ImageVec.begin() + chunkFoundIndex + (chunkSize + 12));
			chunkIndex++; // Increment chunkIndex so that we search again for the same chunk, in case of multiple occurrences.
		}
	}
}

unsigned long updateCrc(const unsigned long& crc, unsigned char* buf, const size_t& len)
{

	unsigned long crcTable[256]{ 0,1996959894,3993919788,2567524794,124634137,1886057615,3915621685,2657392035,249268274,2044508324,3772115230,2547177864,162941995,
					2125561021,3887607047,2428444049,498536548,1789927666,4089016648,2227061214,450548861,1843258603,4107580753,2211677639,325883990,
					1684777152,4251122042,2321926636,335633487,1661365465,4195302755,2366115317,997073096,1281953886,3579855332,2724688242,1006888145,
					1258607687,3524101629,2768942443,901097722,1119000684,3686517206,2898065728,853044451,1172266101,3705015759,2882616665,651767980,
					1373503546,3369554304,3218104598,565507253,1454621731,3485111705,3099436303,671266974,1594198024,3322730930,2970347812,795835527,
					1483230225,3244367275,3060149565,1994146192,31158534,2563907772,4023717930,1907459465,112637215,2680153253,3904427059,2013776290,
					251722036,2517215374,3775830040,2137656763,141376813,2439277719,3865271297,1802195444,476864866,2238001368,4066508878,1812370925,
					453092731,2181625025,4111451223,1706088902,314042704,2344532202,4240017532,1658658271,366619977,2362670323,4224994405,1303535960,
					984961486,2747007092,3569037538,1256170817,1037604311,2765210733,3554079995,1131014506,879679996,2909243462,3663771856,1141124467,
					855842277,2852801631,3708648649,1342533948,654459306,3188396048,3373015174,1466479909,544179635,3110523913,3462522015,1591671054,
					702138776,2966460450,3352799412,1504918807,783551873,3082640443,3233442989,3988292384,2596254646,62317068,1957810842,3939845945,
					2647816111,81470997,1943803523,3814918930,2489596804,225274430,2053790376,3826175755,2466906013,167816743,2097651377,4027552580,
					2265490386,503444072,1762050814,4150417245,2154129355,426522225,1852507879,4275313526,2312317920,282753626,1742555852,4189708143,
					2394877945,397917763,1622183637,3604390888,2714866558,953729732,1340076626,3518719985,2797360999,1068828381,1219638859,3624741850,
					2936675148,906185462,1090812512,3747672003,2825379669,829329135,1181335161,3412177804,3160834842,628085408,1382605366,3423369109,
					3138078467,570562233,1426400815,3317316542,2998733608,733239954,1555261956,3268935591,3050360625,752459403,1541320221,2607071920,
					3965973030,1969922972,40735498,2617837225,3943577151,1913087877,83908371,2512341634,3803740692,2075208622,213261112,2463272603,
					3855990285,2094854071,198958881,2262029012,4057260610,1759359992,534414190,2176718541,4139329115,1873836001,414664567,2282248934,
					4279200368,1711684554,285281116,2405801727,4167216745,1634467795,376229701,2685067896,3608007406,1308918612,956543938,2808555105,
					3495958263,1231636301,1047427035,2932959818,3654703836,1088359270,936918000,2847714899,3736837829,1202900863,817233897,3183342108,
					3401237130,1404277552,615818150,3134207493,3453421203,1423857449,601450431,3009837614,3294710456,1567103746,711928724,3020668471,
					3272380065,1510334235,755167117 };

	unsigned long c = crc;

	for (size_t n{}; n < len; n++) {
		c = crcTable[(c ^ buf[n]) & 0xff] ^ (c >> 8);
	}
	return c;
}

unsigned long crc(unsigned char* buf, const size_t& len)
{
	return updateCrc(0xffffffffL, buf, len) ^ 0xffffffffL;
}

void writeFile(std::vector<unsigned char>& vec, const std::string& OUT_FILE) {

	std::ofstream writeFile(OUT_FILE, std::ios::binary);

	if (!writeFile) {
		std::cerr << "\nWrite Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}

	writeFile.write((char*)&vec[0], vec.size());
	writeFile.close();
}

void insertValue(std::vector<unsigned char>& vec, ptrdiff_t valueInsertIndex, const size_t& VALUE, int bits) {

	while (bits) vec[valueInsertIndex++] = (VALUE >> (bits -= 8)) & 0xff;
}

void displayInfo() {

	std::cout << R"(
PNG Data Vehicle for Reddit, (PDVRDT v1.2). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

PDVRDT enables you to embed & extract arbitrary data of upto ~1MB within a PNG image.
You can then upload and share your data embedded image file on Reddit. 

PDVRDT data embedded images will not work with Twitter. For Twitter, please use PDVZIP.

This program works on Linux and Windows.
 
1,048,444 bytes is the (zlib) uncompressed limit for your arbitrary data.
132 bytes is used for the barebones iCCP profile. (132 + 1048444 = 1,048,576 / 1MB).

To maximise the amount of data you can embed in your image file, I recommend compressing your 
data file(s) to zip/rar formats, etc. Make sure the zip/rar compressed file does not exceed 1,048,444 bytes.

Your file will be encrypted and compressed (deflate/zlib) when embedded into the image file (iCCP Profile chunk).

You can extract embedded content from up to five images at a time.

)";
}
