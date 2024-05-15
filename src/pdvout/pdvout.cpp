void startPdv(std::string& image_file_name) {

	std::ifstream image_ifs(image_file_name, std::ios::binary);

	image_ifs.seekg(0, image_ifs.end);
	const size_t TMP_IMAGE_FILE_SIZE = image_ifs.tellg();
	image_ifs.seekg(0, image_ifs.beg);

	constexpr uint32_t MAX_FILE_SIZE = 209715200;
	constexpr int MIN_FILE_SIZE = 68;

	if (TMP_IMAGE_FILE_SIZE > MAX_FILE_SIZE || MIN_FILE_SIZE > TMP_IMAGE_FILE_SIZE) {
		std::cerr << "\nImage File Error: " << (MIN_FILE_SIZE > TMP_IMAGE_FILE_SIZE ? "Size of image is too small to be a valid PNG image." : "Size of image exceeds the maximum limit of " + std::to_string(MAX_FILE_SIZE) + " Bytes.\n\n");
		std::exit(EXIT_FAILURE);
	}

	std::vector<uchar>Image_Vec((std::istreambuf_iterator<char>(image_ifs)), std::istreambuf_iterator<char>());

	uint32_t image_file_size = static_cast<uint32_t>(Image_Vec.size());

	const std::string
		PNG_HEADER_SIG = "\x89PNG",
		PNG_END_SIG = "IEND\xAE\x42\x60\x82",
		GET_PNG_HEADER_SIG{ Image_Vec.begin(), Image_Vec.begin() + PNG_HEADER_SIG.length() },
		GET_PNG_END_SIG{ Image_Vec.end() - PNG_END_SIG.length(), Image_Vec.end() };

	if (GET_PNG_HEADER_SIG != PNG_HEADER_SIG || GET_PNG_END_SIG != PNG_END_SIG) {
		std::cerr << "\nImage File Error: File does not appear to be a valid PNG image.\n\n";
		std::exit(EXIT_FAILURE);
	}

	constexpr uint8_t ICCP_CHUNK_INDEX = 33;

	constexpr uchar SIG[14]{ 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63, 0x49, 0x44, 0x41, 0x54, 0x5F, 0x78, 0x9c }; 

	const uint32_t
		ICCP_POS = static_cast<uint32_t>(std::search(Image_Vec.begin(), Image_Vec.end(), &SIG[0], &SIG[6]) - Image_Vec.begin() - 4),
		IDAT_POS = static_cast<uint32_t>(std::search(Image_Vec.begin(), Image_Vec.end(), &SIG[7], &SIG[13]) - Image_Vec.begin() - 4);

	if (ICCP_POS != ICCP_CHUNK_INDEX && IDAT_POS == static_cast<uint32_t>(Image_Vec.size() - 4)) {
		std::cerr << "\nImage File Error: This is not a pdvrdt file-embedded image.\n\n";
		std::exit(EXIT_FAILURE);
	}

	bool isMastodonFile = false;

	if (ICCP_POS == ICCP_CHUNK_INDEX) {
		isMastodonFile = true;  
	}

	const uint32_t
		CHUNK_SIZE_INDEX = isMastodonFile ? ICCP_POS : IDAT_POS,				
		DEFLATE_DATA_INDEX = isMastodonFile ? ICCP_POS + 13 : IDAT_POS + 9,		

		CHUNK_SIZE = (static_cast<uint32_t>(Image_Vec[CHUNK_SIZE_INDEX]) << 24) |
					 (static_cast<uint32_t>(Image_Vec[CHUNK_SIZE_INDEX + 1]) << 16) |
					 (static_cast<uint32_t>(Image_Vec[CHUNK_SIZE_INDEX + 2]) << 8) |
					 static_cast<uint32_t>(Image_Vec[CHUNK_SIZE_INDEX + 3]),

		DEFLATE_CHUNK_SIZE = isMastodonFile ? CHUNK_SIZE - 9 : CHUNK_SIZE;

	Image_Vec.erase(Image_Vec.begin(), Image_Vec.begin() + DEFLATE_DATA_INDEX);
	Image_Vec.erase(Image_Vec.begin() + DEFLATE_CHUNK_SIZE, Image_Vec.end());

	inflateFile(Image_Vec);

	if (!Image_Vec.size()) {
		std::cerr << "\nImage File Error: Inflating data chunk failed. Not a pdvrdt file-embedded-image or image file is corrupt.\n\n";
		std::exit(EXIT_FAILURE);
	}

	constexpr uint8_t FILENAME_START_INDEX = 101;

	constexpr uint16_t
		PDV_SIG_START_INDEX = 401,		
		DATA_FILE_START_INDEX = 404;	

	const uint8_t FILENAME_LENGTH = Image_Vec[100];

	std::string data_file_name = { Image_Vec.begin() + FILENAME_START_INDEX, Image_Vec.begin() + FILENAME_START_INDEX + FILENAME_LENGTH };

	const std::string
		PDV_SIG = "PDV",
		GET_PDV_SIG{ Image_Vec.begin() + PDV_SIG_START_INDEX, Image_Vec.begin() + PDV_SIG_START_INDEX + PDV_SIG.length() };

	if (GET_PDV_SIG != PDV_SIG) {
		std::cerr << "\nImage File Error: Signature match failure. This is not a pdvrdt file-embedded image.\n\n";
		std::exit(EXIT_FAILURE);
	}

	Image_Vec.erase(Image_Vec.begin(), Image_Vec.begin() + DATA_FILE_START_INDEX);

	std::vector<uchar> File_Vec;

	decryptFile(Image_Vec, File_Vec, data_file_name);
	
	std::reverse(File_Vec.begin(), File_Vec.end());

	std::ofstream file_ofs(data_file_name, std::ios::binary);

	if (!file_ofs) {
		std::cerr << "\nWrite File Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}
	const uint32_t data_file_size = static_cast<uint32_t>(File_Vec.size());
	file_ofs.write((char*)&File_Vec[0], data_file_size);

	std::cout << "\nExtracted hidden file: " + data_file_name + '\x20' + std::to_string(data_file_size) + " Bytes.\n\nComplete! Please check your file.\n\n";
}