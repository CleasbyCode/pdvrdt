void startPdv(const std::string& IMAGE_FILENAME) {

	const size_t TMP_IMAGE_FILE_SIZE = std::filesystem::file_size(IMAGE_FILENAME);
	
	constexpr uint_fast32_t 
		MAX_FILE_SIZE = 1094713344,
		LARGE_FILE_SIZE = 104857600;

	constexpr uint_fast8_t MIN_FILE_SIZE = 68;

	if (TMP_IMAGE_FILE_SIZE > MAX_FILE_SIZE || MIN_FILE_SIZE > TMP_IMAGE_FILE_SIZE) {
		std::cerr << "\nImage Size Error: " << (MIN_FILE_SIZE > TMP_IMAGE_FILE_SIZE 
			? "Image is too small to be a valid PNG image" 
			: "Image exceeds the maximum limit of " + std::to_string(MAX_FILE_SIZE) + " Bytes") 
		+ ".\n\n";

		std::exit(EXIT_FAILURE);
	}

	std::ifstream image_ifs(IMAGE_FILENAME, std::ios::binary);
	
	if (!image_ifs) {
		std::cerr << "\nOpen File Error: Unable to read image file.\n\n";
		std::exit(EXIT_FAILURE);
	}

	std::cout << (TMP_IMAGE_FILE_SIZE > LARGE_FILE_SIZE ? "\nPlease wait. Larger files will take longer to process.\n" : "");

	std::vector<uint_fast8_t>Image_Vec((std::istreambuf_iterator<char>(image_ifs)), std::istreambuf_iterator<char>());

	constexpr uint_fast8_t
		PNG_SIG[] 	{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A },
		PNG_IEND_SIG[]	{ 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 },
		ICCP_SIG[] 	{ 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63 },
		IDAT_FILE_SIG[] { 0x49, 0x44, 0x41, 0x54, 0x5E, 0x78 };
		
	if (!std::equal(std::begin(PNG_SIG), std::end(PNG_SIG), std::begin(Image_Vec)) 
		|| !std::equal(std::begin(PNG_IEND_SIG), std::end(PNG_IEND_SIG), std::end(Image_Vec) - 8)) {
        		std::cerr << "\nImage File Error: Signature check failure. This file is not a valid PNG image.\n\n";
			std::exit(EXIT_FAILURE);
    	}

	constexpr uint_fast8_t ICCP_CHUNK_INDEX = 0x25;

	const uint_fast32_t
		ICCP_POS = searchFunc(Image_Vec, 0, 0, ICCP_SIG),
		IDAT_POS = searchFunc(Image_Vec, 0, 0, IDAT_FILE_SIG);

	if (ICCP_POS != ICCP_CHUNK_INDEX && IDAT_POS == Image_Vec.size()) {
		std::cerr << "\nImage File Error: This is not a pdvrdt file-embedded image.\n\n";
		std::exit(EXIT_FAILURE);
	}

	bool isMastodonFile = ICCP_POS == ICCP_CHUNK_INDEX ? true : false;
	
	const uint_fast32_t
		CHUNK_SIZE_INDEX = isMastodonFile ? ICCP_POS - 4 : IDAT_POS - 4,				
		DEFLATE_DATA_INDEX = isMastodonFile ? ICCP_POS + 9 : IDAT_POS + 5,		
		CHUNK_SIZE = getFourByteValue(Image_Vec, CHUNK_SIZE_INDEX),
		DEFLATE_CHUNK_SIZE = isMastodonFile ? CHUNK_SIZE - 9 : CHUNK_SIZE;
	
	Image_Vec.erase(Image_Vec.begin(), Image_Vec.begin() + DEFLATE_DATA_INDEX);
	Image_Vec.erase(Image_Vec.begin() + DEFLATE_CHUNK_SIZE, Image_Vec.end());
	
	inflateFile(Image_Vec);

	constexpr uint_fast16_t
		PDV_SIG_INDEX = 0x191,		
		DATA_FILE_INDEX = 0x1A3;

	constexpr uint_fast8_t 
		ENCRYPTED_FILENAME_INDEX = 0x65,
		PDV_SIG[] { 0x50, 0x44, 0x56, 0x52, 0x44, 0x54 };

	if (!std::equal(std::begin(PDV_SIG), std::end(PDV_SIG), std::begin(Image_Vec) + PDV_SIG_INDEX)) {
		std::cerr << "\nImage File Error: Signature match failure. This is not a pdvrdt file-embedded image.\n\n";
		std::exit(EXIT_FAILURE);
	}

	const uint_fast8_t ENCRYPTED_FILENAME_LENGTH = Image_Vec[100];
	
	uint_fast16_t xor_key_index = 0x197;

	uint_fast8_t xor_key[XOR_KEY_LENGTH];
	
	for (int i = 0; i < XOR_KEY_LENGTH;) {
		xor_key[i++] = Image_Vec[xor_key_index++]; 	
    	}

	std::string encrypted_data_filename = { Image_Vec.begin() + ENCRYPTED_FILENAME_INDEX, Image_Vec.begin() + ENCRYPTED_FILENAME_INDEX + ENCRYPTED_FILENAME_LENGTH };

	Image_Vec.erase(Image_Vec.begin(), Image_Vec.begin() + DATA_FILE_INDEX);

	const std::string DECRYPTED_DATA_FILENAME = decryptFile(Image_Vec, xor_key, encrypted_data_filename);	
	
	std::reverse(Image_Vec.begin(), Image_Vec.end());

	uint_fast32_t EMBEDDED_IMAGE_FILE_SIZE = static_cast<uint_fast32_t>(Image_Vec.size());
		
	std::ofstream file_ofs(DECRYPTED_DATA_FILENAME, std::ios::binary);

	if (!file_ofs) {
		std::cerr << "\nWrite File Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}

	file_ofs.write((char*)&Image_Vec[0], EMBEDDED_IMAGE_FILE_SIZE);

	std::cout << "\nExtracted hidden file: " + DECRYPTED_DATA_FILENAME + '\x20' + std::to_string(EMBEDDED_IMAGE_FILE_SIZE) + " Bytes.\n\nComplete! Please check your file.\n\n";
}
