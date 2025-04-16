uint8_t pdvOut(const std::string& IMAGE_FILENAME) {
	std::ifstream image_file_ifs(IMAGE_FILENAME, std::ios::binary);
	
	if (!image_file_ifs) {
		std::cerr << "\nOpen File Error: Unable to read image file.\n\n";
		return 1;
	}

	const uintmax_t IMAGE_FILE_SIZE = std::filesystem::file_size(IMAGE_FILENAME);
	
	std::vector<uint8_t> image_vec;
	image_vec.resize(IMAGE_FILE_SIZE);

	image_file_ifs.read(reinterpret_cast<char*>(image_vec.data()), IMAGE_FILE_SIZE);
	image_file_ifs.close();

	constexpr std::array<uint8_t, 7> 
		PNG_SIG 	{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A }, 
		PNG_IEND_SIG	{ 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60 },
		ICCP_SIG 	{ 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63 },
		PDV_SIG 	{ 0xC6, 0x50, 0x3C, 0xEA, 0x5E, 0x9D, 0xF9 };


	constexpr uint8_t ICCP_CHUNK_INDEX = 0x25;
		
	if (!std::equal(PNG_SIG.begin(), PNG_SIG.end(), image_vec.begin()) || !std::equal(PNG_IEND_SIG.begin(), PNG_IEND_SIG.end(), image_vec.end() - 8)) {
        		std::cerr << "\nImage File Error: Signature check failure. Not a valid PNG image.\n\n";
			return 1;
    	}

	const uint32_t
		ICCP_SIG_POS  = static_cast<uint32_t>(std::search(image_vec.begin(), image_vec.end(), ICCP_SIG.begin(), ICCP_SIG.end()) - image_vec.begin()),
		PDV_SIG_POS   = static_cast<uint32_t>(std::search(image_vec.begin(), image_vec.end(), PDV_SIG.begin(),  PDV_SIG.end()) - image_vec.begin());

	if (ICCP_SIG_POS != ICCP_CHUNK_INDEX && PDV_SIG_POS == image_vec.size()) {
		std::cerr << "\nImage File Error: This is not a pdvrdt image.\n\n";
		return 1;
	}

	bool isMastodonFile = (ICCP_SIG_POS == ICCP_CHUNK_INDEX && PDV_SIG_POS == image_vec.size());
	
	const uint32_t
		CHUNK_SIZE_INDEX = isMastodonFile ? ICCP_SIG_POS - 4 : PDV_SIG_POS - 0x070,				
		PROFILE_DATA_INDEX = isMastodonFile ? ICCP_SIG_POS + 9 : CHUNK_SIZE_INDEX + 0x0B,		
		CHUNK_SIZE = isMastodonFile ? getByteValue<uint32_t>(image_vec, CHUNK_SIZE_INDEX) - 9 : getByteValue<uint32_t>(image_vec, CHUNK_SIZE_INDEX);

	uint8_t byte = image_vec.back();

	image_vec.erase(image_vec.begin(), image_vec.begin() + PROFILE_DATA_INDEX);
	image_vec.erase(image_vec.begin() + (isMastodonFile ? CHUNK_SIZE + 3 : CHUNK_SIZE - 3), image_vec.end());
	
	if (isMastodonFile) {
		inflateFile(image_vec);
		if (image_vec.empty()) {
			std::cerr << "\nFile Size Error: File is zero bytes. Probable failure inflating file.\n\n";
			return 1;
		}
		const uint32_t PDV_ICCP_SIG_POS = static_cast<uint32_t>(std::search(image_vec.begin(), image_vec.end(), PDV_SIG.begin(), PDV_SIG.end()) - image_vec.begin());
		
		if (PDV_ICCP_SIG_POS == image_vec.size()) {
			std::cerr << "\nImage File Error: This is not a pdvrdt image.\n\n";
			return 1;
		}
	}

	constexpr uint32_t LARGE_FILE_SIZE = 300 * 1024 * 1024;  // 400MB.

	if (IMAGE_FILE_SIZE > LARGE_FILE_SIZE) {
		std::cout << "\nPlease wait. Larger files will take longer to process.\n";
	}

	const std::string DECRYPTED_FILENAME = decryptFile(image_vec, isMastodonFile);

	const uint32_t INFLATED_FILE_SIZE = inflateFile(image_vec);

	if (image_vec.empty()) {
		
		std::fstream file(IMAGE_FILENAME, std::ios::in | std::ios::out | std::ios::binary); 
   		 
	    	file.seekg(-1, std::ios::end);
	    	std::streampos byte_pos = file.tellg();

	    	file.read(reinterpret_cast<char*>(&byte), sizeof(byte));

    	    	byte = byte == 0x82 ? 0 : ++byte;

		if (byte > 2) {
			file.close();
			std::ofstream file(IMAGE_FILENAME, std::ios::out | std::ios::trunc | std::ios::binary);
		} else {
			file.seekp(byte_pos);
			file.write(reinterpret_cast<char*>(&byte), sizeof(byte));
		}

		file.close();

    	    	std::cerr << "\nFile Recovery Error: Invalid PIN or file is corrupt.\n\n";

		return 1;
	}

	if (byte != 0x82) {	
		std::fstream file(IMAGE_FILENAME, std::ios::in | std::ios::out | std::ios::binary); 
   		 
		file.seekg(-1, std::ios::end);
		std::streampos byte_pos = file.tellg();

		byte = 0x82;

		file.seekp(byte_pos);
		file.write(reinterpret_cast<char*>(&byte), sizeof(byte));

		file.close();
	}
		
	std::ofstream file_ofs(DECRYPTED_FILENAME, std::ios::binary);

	if (!file_ofs) {
		std::cerr << "\nWrite File Error: Unable to write to file.\n\n";
		return 1;
	}

	file_ofs.write(reinterpret_cast<const char*>(image_vec.data()), INFLATED_FILE_SIZE);

	std::vector<uint8_t>().swap(image_vec);

	std::cout << "\nExtracted hidden file: " << DECRYPTED_FILENAME << " (" << INFLATED_FILE_SIZE << " bytes).\n\nComplete! Please check your file.\n\n";

	return 0;
}