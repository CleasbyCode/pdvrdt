//	PNG Data Vehicle _ Extract (pdvex v1.2). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <zlib.h>

// Open file-embedded PNG image, then proceed to inflate, decrypt and extract the embedded file. 
void processEmbeddedImage(char* []);

// zlib function to inflate (uncompress) iCCP Profile chunk, which includes embedded data file.
void inflate(std::vector<unsigned char>&);

int main(int argc, char** argv) {

	if (argc == 2 && std::string(argv[1]) == "--info") {
		argc = 0;
		std::cout << "\nPNG Data Vehicle _ Extract (pdvex v1.2). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.\n\n" <<
				"Extract embedded data file(s) from PNG image(s) originaly inserted by pdvin.\n" <<
				"You can extract embedded content from up to five images at a time.\n\n";
	}
	else if (argc < 2 || argc > 6) {
		std::cerr << "\nUsage:\tpdvex  <image_1 ... image_5>\n\tpdvex  --info\n\n";
		argc = 0;
	}
	else {
		while (argc !=1 ) {
			processEmbeddedImage(argv++);
			argc--;
			}
	}
	if (argc !=0) std::cout << "\nAll done!\n\n";
	return 0;
}

void processEmbeddedImage(char* argv[]) {

	const std::string IMAGE_FILE = argv[1];
	
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
		plteChunkSize = 0,	// If no PLTE chunk, then this value remains 0.
		plteChunkSizeIndex = 33,
		chunkIndex = 37;	// If PNG-8, chunk index location of PLTE chunk, else chunk index of iCCP Profile. 

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
	
	// Call function to inflate profile chunk, which includes user's data file. 
	inflate(ImageVec);
	
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
	
	// Write data in vector ExtractedFileVec out to file.
	std::ofstream writeFile(decryptedName, std::ios::binary);
	
	if (!writeFile) {
		std::cerr << "\nWrite Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}

	writeFile.write((char*)&ExtractedFileVec[0], ExtractedFileVec.size());

	std::cout << "\nCreated output file: \"" + decryptedName + "\"\n";
}

void inflate(std::vector<unsigned char>& Vec) {

	// zlib function, see https://zlib.net/

	std::vector <unsigned char> Buffer;
	
	constexpr size_t BUFSIZE = 256 * 1024;
	unsigned char* temp_buffer{ new unsigned char[BUFSIZE] };

	z_stream strm;
	strm.zalloc = 0;
	strm.zfree = 0;
	strm.next_in = Vec.data();
	strm.avail_in = Vec.size();
	strm.next_out = temp_buffer;
	strm.avail_out = BUFSIZE;

	inflateInit(&strm);
	
	while (strm.avail_in)
	{
		inflate(&strm, Z_NO_FLUSH);
		
		if (!strm.avail_out)
		{
			Buffer.insert(Buffer.end(), temp_buffer, temp_buffer + BUFSIZE);
			strm.next_out = temp_buffer;
			strm.avail_out = BUFSIZE;
		}
		else
			break;
	}
		inflate(&strm, Z_FINISH);
		Buffer.insert(Buffer.end(), temp_buffer, temp_buffer + BUFSIZE - strm.avail_out);
		inflateEnd(&strm);

	Vec.swap(Buffer);
	delete[] temp_buffer;
}
