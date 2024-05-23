
void startPdv(std::string& image_file_name) {

	std::ifstream image_ifs(image_file_name, std::ios::binary);

	image_ifs.seekg(0, image_ifs.end);
	const size_t TMP_IMAGE_FILE_SIZE = image_ifs.tellg();
	image_ifs.seekg(0, image_ifs.beg);

	constexpr uint_fast32_t 
		MAX_FILE_SIZE = 209715200,
		LARGE_FILE_SIZE = 52428800;

	constexpr uint_fast8_t MIN_FILE_SIZE = 68;

	if (TMP_IMAGE_FILE_SIZE > MAX_FILE_SIZE || MIN_FILE_SIZE > TMP_IMAGE_FILE_SIZE) {
		std::cerr << "\nImage File Error: " << (MIN_FILE_SIZE > TMP_IMAGE_FILE_SIZE ? "Size of image is too small to be a valid PNG image." : "Size of image exceeds the maximum limit of " + std::to_string(MAX_FILE_SIZE) + " Bytes.\n\n");
		std::exit(EXIT_FAILURE);
	}

	if (TMP_IMAGE_FILE_SIZE > LARGE_FILE_SIZE) {
		std::cout << "\nPlease Wait. Large files will take longer to process.\n";
	}

	std::vector<uint_fast8_t>Image_Vec((std::istreambuf_iterator<char>(image_ifs)), std::istreambuf_iterator<char>());

	uint_fast32_t image_file_size = static_cast<uint_fast32_t>(Image_Vec.size());

	constexpr uint_fast8_t
		PNG_SIG[8] 	 { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A },
		PNG_IEND_SIG[8]	 { 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 },
		ICCP_SIG[7] 	 { 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63 },
		IDAT_FILE_SIG[6] { 0x49, 0x44, 0x41, 0x54, 0x5E, 0x78 };
		
	if (!std::equal(std::begin(PNG_SIG), std::end(PNG_SIG), std::begin(Image_Vec)) || !std::equal(std::begin(PNG_IEND_SIG), std::end(PNG_IEND_SIG), std::end(Image_Vec) - 8)) {
        		std::cerr << "\nImage File Error: Signature check failure. This file is not a valid PNG image.\n\n";
			std::exit(EXIT_FAILURE);
    	}

	constexpr uint_fast8_t ICCP_CHUNK_INDEX = 0x25;

	const uint_fast32_t
		ICCP_POS = static_cast<uint_fast32_t>(std::search(Image_Vec.begin(), Image_Vec.end(), std::begin(ICCP_SIG), std::end(ICCP_SIG)) - Image_Vec.begin()),
		IDAT_POS = static_cast<uint_fast32_t>(std::search(Image_Vec.begin(), Image_Vec.end(), std::begin(IDAT_FILE_SIG), std::end(IDAT_FILE_SIG)) - Image_Vec.begin());

	if (ICCP_POS != ICCP_CHUNK_INDEX && IDAT_POS == Image_Vec.size()) {
		std::cerr << "\nImage File Error: This is not a pdvrdt file-embedded image.\n\n";
		std::exit(EXIT_FAILURE);
	}

	bool isMastodonFile = false;

	if (ICCP_POS == ICCP_CHUNK_INDEX) {
		isMastodonFile = true;  
	}

	const uint_fast32_t
		CHUNK_SIZE_INDEX = isMastodonFile ? ICCP_POS - 4 : IDAT_POS - 4,				
		DEFLATE_DATA_INDEX = isMastodonFile ? ICCP_POS + 9 : IDAT_POS + 5,		

		CHUNK_SIZE = (static_cast<uint_fast32_t>(Image_Vec[CHUNK_SIZE_INDEX]) << 24) |
				(static_cast<uint_fast32_t>(Image_Vec[CHUNK_SIZE_INDEX + 1]) << 16) |
				(static_cast<uint_fast32_t>(Image_Vec[CHUNK_SIZE_INDEX + 2]) << 8) |
				static_cast<uint_fast32_t>(Image_Vec[CHUNK_SIZE_INDEX + 3]),

		DEFLATE_CHUNK_SIZE = isMastodonFile ? CHUNK_SIZE - 9 : CHUNK_SIZE;

	Image_Vec.erase(Image_Vec.begin(), Image_Vec.begin() + DEFLATE_DATA_INDEX);
	Image_Vec.erase(Image_Vec.begin() + DEFLATE_CHUNK_SIZE, Image_Vec.end());

	inflateFile(Image_Vec);

	if (!Image_Vec.size()) {
		std::cerr << "\nImage File Error: Inflating data chunk failed. Not a pdvrdt file-embedded-image or image file is corrupt.\n\n";
		std::exit(EXIT_FAILURE);
	}

	constexpr uint_fast16_t
		PDV_SIG_INDEX = 0x191,		
		DATA_FILE_INDEX = 0x194;	

	const uint_fast8_t FILENAME_LENGTH = Image_Vec[100];

	constexpr uint_fast8_t 
		FILENAME_INDEX = 0x65,
		PDV_SIG[3] { 0x50, 0x44, 0x56 };

	std::string data_file_name = { Image_Vec.begin() + FILENAME_INDEX, Image_Vec.begin() + FILENAME_INDEX + FILENAME_LENGTH };

	if (!std::equal(std::begin(PDV_SIG), std::end(PDV_SIG), std::begin(Image_Vec) + PDV_SIG_INDEX)) {
		std::cerr << "\nImage File Error: Signature match failure. This is not a pdvrdt file-embedded image.\n\n";
		std::exit(EXIT_FAILURE);
	}

	Image_Vec.erase(Image_Vec.begin(), Image_Vec.begin() + DATA_FILE_INDEX);

	std::vector<uint_fast8_t> File_Vec;
	File_Vec.reserve(Image_Vec.size());	

	decryptFile(Image_Vec, File_Vec, data_file_name);
	
	std::reverse(File_Vec.begin(), File_Vec.end());

	std::ofstream file_ofs(data_file_name, std::ios::binary);

	if (!file_ofs) {
		std::cerr << "\nWrite File Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}
	const uint_fast32_t data_file_size = static_cast<uint_fast32_t>(File_Vec.size());
	file_ofs.write((char*)&File_Vec[0], data_file_size);

	std::cout << "\nExtracted hidden file: " + data_file_name + '\x20' + std::to_string(data_file_size) + " Bytes.\n\nComplete! Please check your file.\n\n";
}
