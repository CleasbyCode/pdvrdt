int pdvOut(const std::string& IMAGE_FILENAME) {

	constexpr uint32_t 
		MAX_FILE_SIZE 	= 3U * 1024U * 1024U * 1024U, 	// 3GB.
		LARGE_FILE_SIZE = 400 * 1024 * 1024;  		// 400MB.

	constexpr uint8_t MIN_FILE_SIZE = 68;

	const size_t IMAGE_FILE_SIZE = std::filesystem::file_size(IMAGE_FILENAME);

	if (IMAGE_FILE_SIZE > MAX_FILE_SIZE || MIN_FILE_SIZE > IMAGE_FILE_SIZE) {
		std::cerr << "\nImage Size Error: " << (MIN_FILE_SIZE > IMAGE_FILE_SIZE 
			? "Image is too small to be a valid PNG image" 
			: "Image exceeds the maximum limit of " + std::to_string(MAX_FILE_SIZE) + " Bytes") 
		+ ".\n\n";
		return 1;
	}

	std::ifstream image_file_ifs(IMAGE_FILENAME, std::ios::binary);
	
	if (!image_file_ifs) {
		std::cerr << "\nOpen File Error: Unable to read image file.\n\n";
		return 1;
	}

	if (IMAGE_FILE_SIZE > LARGE_FILE_SIZE) {
		std::cout << "\nPlease wait. Larger files will take longer to process.\n";
	}

	std::vector<uint8_t> Image_Vec;
	Image_Vec.resize(IMAGE_FILE_SIZE);

	image_file_ifs.read(reinterpret_cast<char*>(Image_Vec.data()), IMAGE_FILE_SIZE);

	constexpr uint8_t
		PNG_SIG[] 	{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A },
		PNG_IEND_SIG[]	{ 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 },
		ICCP_SIG[] 	{ 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63 },
		IDAT_FILE_SIG[] { 0x49, 0x44, 0x41, 0x54, 0x5E, 0x78 },
		PDV_SIG[] 	{ 0x50, 0x44, 0x56, 0x52, 0x44, 0x54 };
		
	if (!std::equal(std::begin(PNG_SIG), std::end(PNG_SIG), std::begin(Image_Vec)) || !std::equal(std::begin(PNG_IEND_SIG), std::end(PNG_IEND_SIG), std::end(Image_Vec) - 8)) {
        	std::cerr << "\nImage File Error: Signature check failure. This file is not a valid PNG image.\n\n";
		return 1;
    	}

	constexpr uint8_t ICCP_CHUNK_INDEX = 0x25;

	const uint32_t
		ICCP_POS  = static_cast<uint32_t>(std::search(Image_Vec.begin(), Image_Vec.end(), std::begin(ICCP_SIG), std::end(ICCP_SIG)) - Image_Vec.begin()),
		IDAT_POS = static_cast<uint32_t>(std::search(Image_Vec.begin(), Image_Vec.end(), std::begin(IDAT_FILE_SIG), std::end(IDAT_FILE_SIG)) - Image_Vec.begin());

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

	constexpr uint16_t PDV_SIG_INDEX = 0x191;
		
	if (!std::equal(std::begin(PDV_SIG), std::end(PDV_SIG), std::begin(Image_Vec) + PDV_SIG_INDEX)) {
		std::cerr << "\nImage File Error: Signature match failure. This is not a pdvrdt file-embedded image.\n\n";
		return 1;
	}

	const std::string DECRYPTED_FILENAME = decryptFile(Image_Vec);	
				 
	std::reverse(Image_Vec.begin(), Image_Vec.end());
		
	std::ofstream file_ofs(DECRYPTED_FILENAME, std::ios::binary);

	if (!file_ofs) {
		std::cerr << "\nWrite File Error: Unable to write to file.\n\n";
		return 1;
	}

	const uint32_t FILE_SIZE = static_cast<uint32_t>(Image_Vec.size());

	file_ofs.write(reinterpret_cast<const char*>(Image_Vec.data()), FILE_SIZE);

	std::vector<uint8_t>().swap(Image_Vec);

	std::cout << "\nExtracted hidden file: " << DECRYPTED_FILENAME << " (" << FILE_SIZE << " bytes).\n\nComplete! Please check your file.\n\n";

	return 0;
}
