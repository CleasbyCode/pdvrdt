//	PNG Data Vehicle for Reddit, (PDVRDT v1.3). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <zlib.h>

// Open user image & data file and check file size requirements. Display error & exit program if any file fails to open or exceeds size limits.
void processFiles(char* [], int, int, const std::string&);

// Open pdvrdt PNG image file, then proceed to inflate/uncompress, decrypt and extract embedded data file from it. 
void processEmbeddedImage(char* []);

// Read in and store PNG image & data file into vectors. Data file is encrypted and deflate/compressed (zlib).
void readFilesIntoVectors(std::ifstream&, std::ifstream&, const std::string&, const std::string&, const ptrdiff_t&, const ptrdiff_t&, int, int);

// Search and remove all unnecessary PNG chunks found before the first IDAT chunk.
void eraseChunks(std::vector<unsigned char>&);

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
	else if (argc >= 4 && argc < 9 && std::string(argv[1]) == "-i") {
			int sub = argc - 1;	
			argc -= 2;
			const std::string IMAGE_FILE = argv[2];
			while (argc != 1) { 
				processFiles(argv++, argc, sub, IMAGE_FILE);
				argc--;
			}
			argc = 1;
	}
	else if (argc >= 3 && argc < 8 && std::string(argv[1]) == "-x") {
			while (argc >=3 ) {
				processEmbeddedImage(argv++);
				argc--;
			}
	} 
	else {
		std::cerr << "\nUsage:\t\bpdvrdt -i <png-image>  <file(s)>\n\t\bpdvrdt -x <png-image(s)>\n\t\bpdvrdt --info\n\n";
		argc = 0;
	}
	if (argc !=0) {
		if (argc == 2) {
			std::cout << "\nComplete!\n\n";
		}
		else {
			std::cout << "\nComplete!\n\nYou can now post your file-embedded PNG image(s) on reddit.\n\n";
		}
	}
	return 0;
}

void processFiles(char* argv[], int argc, int sub, const std::string& IMAGE_FILE) {

	const std::string DATA_FILE = argv[3];

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
		MAX_PNG_SIZE_BYTES = 5242880,			// Reddit PNG size limit 5MB.
		MAX_DATAFILE_SIZE_BYTES = 1048444;		// Data file size limit for reddit. 
		// MAX_DATAFILE_SIZE_BYTES_IMGUR = 5242880	// Data file size limit for imgur.
		
	// Get size of files.
	readImage.seekg(0, readImage.end),
	readFile.seekg(0, readFile.end);

	const ptrdiff_t
		IMAGE_SIZE = readImage.tellg(),
		DATA_SIZE = readFile.tellg();

	if ((IMAGE_SIZE + DATA_SIZE) > MAX_PNG_SIZE_BYTES
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
	readFilesIntoVectors(readImage, readFile, IMAGE_FILE, DATA_FILE, IMAGE_SIZE, DATA_SIZE, argc, sub);
}

// Inflate, decrypt and extract embedded data file from PNG image.
void processEmbeddedImage(char* argv[]) {

	const std::string IMAGE_FILE = argv[2];
	
	if (IMAGE_FILE == "." || IMAGE_FILE == "/") std::exit(EXIT_FAILURE);
	
	std::ifstream readImage(IMAGE_FILE, std::ios::binary);
	
	if (!readImage) {
		std::cerr << "\nRead Error: Unable to open/read file: \"" +IMAGE_FILE + "\"\n\n";
		std::exit(EXIT_FAILURE);
	}

	// Read file-embedded PNG image into vector "ImageVec".
	std::vector<unsigned char> ImageVec((std::istreambuf_iterator<char>(readImage)), std::istreambuf_iterator<char>());
	
	// When reddit encodes the uploaded PNG image, if PNG-8, it will write the PLTE chunk before the iCCP Profile chunk. We need to check for this.
	int
		plteChunkSize = 0,		// If no PLTE chunk, then this value remains 0.
		plteChunkSizeIndex = 33,
		chunkIndex = 37;	  	// If PNG-8, chunk index location of PLTE chunk, else chunk index of iCCP Profile. 

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
	
	int
		profileChunkSizeIndex = 33 + plteChunkSize, // Start index location of 4 byte iCCP Profile chunk length field.
		// Get iCCP Profile chunk length.
		profileChunkSize = (ImageVec[profileChunkSizeIndex + 1] << 16) | ImageVec[profileChunkSizeIndex + 2] << 8 | ImageVec[profileChunkSizeIndex + 3],
		deflateDataIndex = 54 + plteChunkSize, // Start index location of deflate data within iCCP Profile chunk.
		deflateChunkSize = profileChunkSize - 13;

	// From "ImageVec" vector index 0, erase bytes so that start of vector is now the beginning of the deflate data (78,9C...).
	ImageVec.erase(ImageVec.begin(), ImageVec.begin() + deflateDataIndex);

	// Erase all bytes of "ImageVec" after end of deflate data. Vector should now just contain the deflate data.
	ImageVec.erase(ImageVec.begin() + deflateChunkSize, ImageVec.end());
	
	bool inflate = true;
	
	// Call function to inflate profile chunk, which includes user's data file. 
	inflateDeflate(ImageVec, inflate);
	
	const std::string
		PDV_SIG = "PDVRDT",
		PDV_CHECK{ ImageVec.begin() + 5, ImageVec.begin() + 5 + PDV_SIG.length() },		// Get PDVRDT signature from vector "ImageVec".
		ENCRYPTED_NAME = { ImageVec.begin() + 13, ImageVec.begin() + 13 + ImageVec[12] };	// Get encrypted embedded filename from vector "ImageVec".

	// Make sure this is a pdvrdt file-embedded image.
	if (PDV_CHECK != PDV_SIG) {
		// File requirements check failure, display relevant error message and exit program.
		std::cerr << "\nProfile Error: iCCP profile does not seem to be a PDVRDT modified profile.\n\n";
		std::exit(EXIT_FAILURE);
	}

	// We will store the decrypted embedded file in this vector.
	std::vector<unsigned char> ExtractedFileVec;

	std::string decryptedName;

	bool nameDecrypted = false;
	
	int 
		encryptedNameLength = ImageVec[12],
		keyStartPos = 41 + encryptedNameLength,
		keyLength = 7,
		keyPos = keyStartPos;

	// Decrypt the embedded filename and the embedded data file.
	for (int i = 0; ImageVec.size() > i+132; i++) {		
		if (!nameDecrypted) {
			keyPos = keyPos > keyStartPos + keyLength ? keyStartPos : keyPos;
			decryptedName += ENCRYPTED_NAME[i] ^ ImageVec[keyPos];
		}
		
		ExtractedFileVec.insert(ExtractedFileVec.begin() + i, ImageVec[i+132] ^ ImageVec[keyPos++]);
		
		if (i >= encryptedNameLength-1) {
			nameDecrypted = true;
			keyPos = keyPos > keyStartPos + keyLength ? keyStartPos : keyPos;
		}
	}

	if (decryptedName.substr(0,4) != "pdv_") {
		decryptedName = "pdv_" + decryptedName;
	}
	
	// Write data from vector ExtractedFileVec out to file.
	std::ofstream writeFile(decryptedName, std::ios::binary);
	
	if (!writeFile) {
		std::cerr << "\nWrite Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}

	writeFile.write((char*)&ExtractedFileVec[0], ExtractedFileVec.size());

	std::cout << "\nCreated output file: \"" + decryptedName + "\"\n";
	
}

void readFilesIntoVectors(std::ifstream& readImage, std::ifstream& readFile, const std::string& IMAGE_FILE, const std::string& DATA_FILE, const ptrdiff_t& IMAGE_SIZE, const ptrdiff_t& DATA_SIZE, int argc, int sub) {

	// Reset position of files. 
	readImage.seekg(0, readImage.beg),
	readFile.seekg(0, readFile.beg);

	// User's data file is first inserted and stored at the end of this uncompressed iCCP Profile.
	// The first 132 bytes of this vector contains the barebones profile.
	// Without this basic profile, some image display programs will show error messages when loading the image.
	std::vector<unsigned char>
		ProfileDataVec{
			0x00, 0x00, 0x00, 0x84, 0x20, 0x50, 0x44, 0x56, 0x52, 0x44, 0x54, 0x20,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x61, 0x63, 0x73, 0x70, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	},
		// The deflate (zlib compressed) iCCP Profile + data file, will be inserted in the "ProfileChunkVec" vector, within the PNG iCCP Profile chunk.
		ProfileChunkVec
	{
			0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x49, 0x43, 0x43, 0x20,
			0x50, 0x72, 0x6F, 0x66, 0x69, 0x6C, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00 },

		// Read-in user PNG image file and store in vector "ImageVec".
		ImageVec((std::istreambuf_iterator<char>(readImage)), std::istreambuf_iterator<char>());

	const std::string 
		TXT_NUM = std::to_string(sub - argc),
		EMBEDDED_IMAGE_FILE = "pdvimg"+TXT_NUM+".png",
		PNG_SIG = "\x89PNG",
		PNG_CHECK{ ImageVec.begin(), ImageVec.begin() + PNG_SIG.length() };	// Get image header from vector. 
											
	// Make sure image has valid PNG header.
	if (PNG_CHECK != PNG_SIG) {
		// File requirements check failure, display relevant error message and exit program.
		std::cerr << "\nImage Error: File does not appear to be a valid PNG image.\n\n";
		std::exit(EXIT_FAILURE);
	}

	// Find and remove unwanted chunks.
	eraseChunks(ImageVec);

	const int MAX_LENGTH_FILENAME = 23;

	// We don't want "./" or ".\" characters at the start of the filename (user's data file), which we will store in the iCCP Profile. 
	std::size_t firstSlashPos = DATA_FILE.find_first_of("\\/");
	std::string 
		noSlashName = DATA_FILE.substr(firstSlashPos + 1, DATA_FILE.length()),
		encryptedName;
	
	ptrdiff_t
		profileChunkSizeIndex = 1,	// Start index of the PNG iCCP Profile chunk's 4 byte length field (only 3 bytes used).
		profileNameLengthIndex = 12,	// Index location inside the iCCP Profile to store the length value of the embedded data's filename (1 byte).
		profileNameIndex = 13,		// Start index inside the iCCP Profile to store the filename for the embedded data file.
		noSlashNameLength = noSlashName.length(); // Character length of filename for the embedded data file.

	// Make sure character length of filename (user's data file) does not exceed set maximum.
	if (noSlashNameLength > MAX_LENGTH_FILENAME) {
		std::cerr << "\nFile Error: Filename length of your data file (" + std::to_string(noSlashNameLength) + " characters) is too long.\n"
				"\nFor compatibility requirements, your filename must be under 24 characters.\nPlease try again with a shorter filename.\n\n";
		std::exit(EXIT_FAILURE);
	}

	// Insert the character length value of the filename (user's data file) into the iCCP Profile,
	// We need to know how many characters to read when later retrieving the filename from the profile.
	insertValue(ProfileDataVec, profileNameLengthIndex, noSlashNameLength, 8);

	// Make space for this data by removing equivalent length of characters from iCCP Profile.
	ProfileDataVec.erase(ProfileDataVec.begin() + profileNameIndex, ProfileDataVec.begin() + noSlashNameLength + profileNameIndex);

	int 
		idatCrcPos = ImageVec.size() - 16,
		profileInsertPos =  41,
		xorKeyStartPos = 41,
		xorKeyLength = 7,
		xorKeyIndex = 41,
		crcPos = 29;
		
	// Store the values of the IHDR crc & last IDAT crc (total 8-bytes) into our Profile.
	// We will use these 8 bytes for the xor key.	
	for (int i = 0; i != 9; i++) {
		ProfileDataVec[profileInsertPos++] = ImageVec[crcPos++];
		if (i >= 3) crcPos = idatCrcPos++;
		}

	// Encrypt filename.
	for (int i = 0 ; noSlashNameLength != 0; noSlashNameLength--) {
		xorKeyIndex = xorKeyIndex > xorKeyStartPos + xorKeyLength ? xorKeyStartPos : xorKeyIndex;  // Reset position.
		encryptedName += noSlashName[i++] ^ ProfileDataVec[xorKeyIndex++]; // xor each character of filename against each byte (8) of the crc values.
	}

	// Insert the encrypted filename into the basic iCCP Profile.
	ProfileDataVec.insert(ProfileDataVec.begin() + profileNameIndex, encryptedName.begin(), encryptedName.end());

	char byte;
	
	xorKeyStartPos += encryptedName.length();
	
	for (int xorKeyIndex = xorKeyStartPos, insertIndex = 132; DATA_SIZE + 132 > insertIndex ;) {
		byte = readFile.get();
		ProfileDataVec.insert((ProfileDataVec.begin() + insertIndex++), byte ^ ProfileDataVec[xorKeyIndex++]);	
		xorKeyIndex = xorKeyIndex > xorKeyStartPos + xorKeyLength ? xorKeyStartPos : xorKeyIndex; // Reset position.
	}
	
	bool inflate = false;
	
	// Call function for deflate/compress the contents of vector "ProfileDataVec" (Basic profile with user's (encrypted) data file).
	inflateDeflate(ProfileDataVec, inflate);

	const ptrdiff_t
		PROFILE_DEFLATE_SIZE = ProfileDataVec.size(),
		PROFILE_CHUNK_SIZE = PROFILE_DEFLATE_SIZE + 17, // "iCCPICC Profile\x00\x00" (17 chars).
		PROFILE_DATA_INSERT_INDEX = 21,   // Index location within ProfileChunkVec to insert the deflate contents of ProfileDataVec. 		   
		PROFILE_CHUNK_INSERT_INDEX = 33;  // Index location within ImageVec (image file) to insert ProfileChunkVec (iCCP Profile chunk).
		
	// Insert the deflate/compressed iCCP Profile (with user's data file) into vector "ProfileChunkVec", begining at index location 21.
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

	// Write out to file the PNG image with the embedded (compressed & encrypted) user's data file.
	std::ofstream writeFile(EMBEDDED_IMAGE_FILE, std::ios::binary);

	if (!writeFile) {
		std::cerr << "\nWrite Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}

	writeFile.write((char*)&ImageVec[0], ImageVec.size());
	
	std::cout << "\nCreated output file: \"" + EMBEDDED_IMAGE_FILE + "\"\n";
}

void inflateDeflate(std::vector<unsigned char>& Vec, bool inflateData) {

	// zlib function, see https://zlib.net/

	std::vector <unsigned char> Buffer;

	size_t BUFSIZE;
	
	if (!inflateData) {
		BUFSIZE = 1032 * 1024;  // Required for deflate. This BUFSIZE covers us to our max file size of 1MB. A lower BUFSIZE results in lost data.
	}
	else {
		BUFSIZE = 256 * 1024;  // Fine for inflate.
	}
	
	unsigned char* temp_buffer{ new unsigned char[BUFSIZE] };
  
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
	delete[] temp_buffer;
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

void insertValue(std::vector<unsigned char>& vec, ptrdiff_t valueInsertIndex, const size_t& VALUE, int bits) {

	while (bits) vec[valueInsertIndex++] = (VALUE >> (bits -= 8)) & 0xff;
}

void displayInfo() {

	std::cout << R"(
PNG Data Vehicle for Reddit, (pdvrdt v1.3). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

PDVRDT enables you to embed & extract arbitrary data of upto ~1MB within a PNG image.
You can then upload and share your data embedded image file on Reddit. 

PDVRDT data embedded images will not work with Twitter. For Twitter, please use pdvzip.

This program works on Linux and Windows.
 
1,048,444 bytes is the uncompressed (zlib inflate) limit for your data file.
132 bytes is used for the basic iCCP profile. (132 + 1048444 = 1,048,576 [1MB]).

To maximise the amount of data you can embed in your image file. I recommend compressing your 
data file(s) to zip/rar formats, etc. Make sure the compressed file does not exceed 1,048,444 bytes.

Your file will be encrypted and compressed (zlib deflate) when embedded into the image file.

You can insert up to five files at a time (outputs one image per file).
You can extract files from up to five images at a time.

)";
}
