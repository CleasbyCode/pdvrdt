uint8_t pdvOut(const std::string& IMAGE_FILENAME) {

	constexpr uint32_t 
		MAX_FILE_SIZE 	= 2684354560, // 2.5GB.
		LARGE_FILE_SIZE = 419430400;  // 400MB.

	constexpr uint8_t MIN_FILE_SIZE = 68;

	const size_t IMAGE_FILE_SIZE = std::filesystem::file_size(IMAGE_FILENAME);

	if (IMAGE_FILE_SIZE > MAX_FILE_SIZE || MIN_FILE_SIZE > IMAGE_FILE_SIZE) {
		std::cerr << "\nImage Size Error: " << (MIN_FILE_SIZE > IMAGE_FILE_SIZE 
			? "Image is too small to be a valid PNG image" 
			: "Image exceeds the maximum limit of " + std::to_string(MAX_FILE_SIZE) + " Bytes") 
		+ ".\n\n";
		return 1;
	}

	std::ifstream image_ifs(IMAGE_FILENAME, std::ios::binary);
	
	if (!image_ifs) {
		std::cerr << "\nOpen File Error: Unable to read image file.\n\n";
		return 1;
	}

	if (IMAGE_FILE_SIZE > LARGE_FILE_SIZE) {
		std::cout << "\nPlease wait. Larger files will take longer to process.\n";
	}

	std::vector<uint8_t> Image_Vec;
	Image_Vec.reserve(IMAGE_FILE_SIZE);

	std::copy(std::istreambuf_iterator<char>(image_ifs), std::istreambuf_iterator<char>(), std::back_inserter(Image_Vec));

	constexpr uint8_t
		PNG_SIG[] 	{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A },
		PNG_IEND_SIG[]	{ 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 },
		ICCP_SIG[] 	{ 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63 },
		IDAT_FILE_SIG[] { 0x49, 0x44, 0x41, 0x54, 0x5E, 0x78 };
		
	if (!std::equal(std::begin(PNG_SIG), std::end(PNG_SIG), std::begin(Image_Vec)) || !std::equal(std::begin(PNG_IEND_SIG), std::end(PNG_IEND_SIG), std::end(Image_Vec) - 8)) {
        	std::cerr << "\nImage File Error: Signature check failure. This file is not a valid PNG image.\n\n";
		return 1;
    	}

	constexpr uint8_t ICCP_CHUNK_INDEX = 0x25;

	const uint32_t
		ICCP_POS = searchFunc(Image_Vec, 0, 0, ICCP_SIG),
		IDAT_POS = searchFunc(Image_Vec, 0, 0, IDAT_FILE_SIG);

	if (ICCP_POS != ICCP_CHUNK_INDEX && IDAT_POS == static_cast<uint32_t>(Image_Vec.size())) {
		std::cerr << "\nImage File Error: This is not a pdvrdt file-embedded image.\n\n";
		return 1;
	}

	bool isMastodonFile = (ICCP_POS == ICCP_CHUNK_INDEX);
	
	const uint32_t
		CHUNK_SIZE_INDEX = isMastodonFile ? ICCP_POS - 4 : IDAT_POS - 4,				
		DEFLATE_DATA_INDEX = isMastodonFile ? ICCP_POS + 9 : IDAT_POS + 5,		
		CHUNK_SIZE = getByteValue(Image_Vec, CHUNK_SIZE_INDEX),
		DEFLATE_CHUNK_SIZE = isMastodonFile ? CHUNK_SIZE - 9 : CHUNK_SIZE;
	
	Image_Vec.erase(Image_Vec.begin(), Image_Vec.begin() + DEFLATE_DATA_INDEX);
	Image_Vec.erase(Image_Vec.begin() + DEFLATE_CHUNK_SIZE, Image_Vec.end());
	
	inflateFile(Image_Vec);

	if (Image_Vec.empty()) {
		std::cerr << "\nFile Size Error: File is zero bytes. Probable failure uncompressing file.\n\n";
		return 1;
	}

	constexpr uint16_t
		PDV_SIG_INDEX = 0x191,		
		DATA_FILE_INDEX = 0x1AF;

	constexpr uint8_t 
		XOR_KEY_LENGTH = 24,		 
		ENCRYPTED_FILENAME_INDEX = 0x65,
		PDV_SIG[] { 0x50, 0x44, 0x56, 0x52, 0x44, 0x54 };

	if (!std::equal(std::begin(PDV_SIG), std::end(PDV_SIG), std::begin(Image_Vec) + PDV_SIG_INDEX)) {
		std::cerr << "\nImage File Error: Signature match failure. This is not a pdvrdt file-embedded image.\n\n";
		return 1;
	}

	uint16_t xor_key_index = 0x197;

	uint8_t encrypted_filename_length = Image_Vec[ENCRYPTED_FILENAME_INDEX - 1];
	uint8_t* Xor_Key_Arr = new uint8_t[XOR_KEY_LENGTH];

	// Get the xor_key from the profile.
	for (uint8_t i = 0; XOR_KEY_LENGTH > i; ++i) {
		Xor_Key_Arr[i] = Image_Vec[xor_key_index++]; 
	}
				 
	const std::string ENCRYPTED_FILENAME = { Image_Vec.begin() + ENCRYPTED_FILENAME_INDEX, Image_Vec.begin() + ENCRYPTED_FILENAME_INDEX + encrypted_filename_length };

	Image_Vec.erase(Image_Vec.begin(), Image_Vec.begin() + DATA_FILE_INDEX);

	const uint32_t FILE_SIZE = static_cast<uint32_t>(Image_Vec.size());

	const std::string DECRYPTED_FILENAME = decryptFile(Image_Vec, Xor_Key_Arr, ENCRYPTED_FILENAME);	
	
	delete[] Xor_Key_Arr;
				 
	std::reverse(Image_Vec.begin(), Image_Vec.end());
		
	std::ofstream file_ofs(DECRYPTED_DATA_FILENAME, std::ios::binary);

	if (!file_ofs) {
		std::cerr << "\nWrite File Error: Unable to write to file.\n\n";
		return 1;
	}

	file_ofs.write((char*)&Image_Vec[0], FILE_SIZE);

	std::vector<uint8_t>().swap(Image_Vec);

	std::cout << "\nExtracted hidden file: " + DECRYPTED_DATA_FILENAME + '\x20' + std::to_string(FILE_SIZE) + " Bytes.\n\nComplete! Please check your file.\n\n";

	return 0;
}
