int pdvOut(const std::string& IMAGE_FILENAME) {

	constexpr uint32_t 
		MAX_FILE_SIZE 	= 3U * 1024U * 1024U * 1024U, 	
		LARGE_FILE_SIZE = 400 * 1024 * 1024;  		

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

	std::vector<uint8_t> Image_Vec;
	Image_Vec.resize(IMAGE_FILE_SIZE);

	image_file_ifs.read(reinterpret_cast<char*>(Image_Vec.data()), IMAGE_FILE_SIZE);
	image_file_ifs.close();

	constexpr uint8_t
		PNG_SIG[] 	{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A },
		PNG_IEND_SIG[]	{ 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60 },
		ICCP_SIG[] 	{ 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63 },
		PDV_SIG[] 	{ 0xC6, 0x50, 0x3C, 0xEA, 0x5E, 0x9D, 0xF9 },
		ICCP_CHUNK_INDEX = 0x25;
		
	if (!std::equal(std::begin(PNG_SIG), std::end(PNG_SIG), std::begin(Image_Vec)) || !std::equal(std::begin(PNG_IEND_SIG), std::end(PNG_IEND_SIG), std::end(Image_Vec) - 8)) {
        	std::cerr << "\nImage File Error: Signature check failure. This file is not a valid PNG image.\n\n";
		return 1;
    	}

	const uint32_t
		ICCP_SIG_POS  = static_cast<uint32_t>(std::search(Image_Vec.begin(), Image_Vec.end(), std::begin(ICCP_SIG), std::end(ICCP_SIG)) - Image_Vec.begin()),
		PDV_SIG_POS = static_cast<uint32_t>(std::search(Image_Vec.begin(), Image_Vec.end(), std::begin(PDV_SIG), std::end(PDV_SIG)) - Image_Vec.begin());

	if (ICCP_SIG_POS != ICCP_CHUNK_INDEX && PDV_SIG_POS == Image_Vec.size()) {
		std::cerr << "\nImage File Error: This is not a pdvrdt image.\n\n";
		return 1;
	}

	bool isMastodonFile = (ICCP_SIG_POS == ICCP_CHUNK_INDEX && PDV_SIG_POS == Image_Vec.size());
	
	const uint32_t
		CHUNK_SIZE_INDEX = isMastodonFile ? ICCP_SIG_POS - 4 : PDV_SIG_POS - 0x070,				
		PROFILE_DATA_INDEX = isMastodonFile ? ICCP_SIG_POS + 9 : CHUNK_SIZE_INDEX + 0x0B,		
		CHUNK_SIZE = isMastodonFile ? getByteValue<uint32_t>(Image_Vec, CHUNK_SIZE_INDEX) - 9 : getByteValue<uint32_t>(Image_Vec, CHUNK_SIZE_INDEX);

	uint8_t byte = Image_Vec.back();

	Image_Vec.erase(Image_Vec.begin(), Image_Vec.begin() + PROFILE_DATA_INDEX);
	Image_Vec.erase(Image_Vec.begin() + ( isMastodonFile ? CHUNK_SIZE + 3 : CHUNK_SIZE - 3), Image_Vec.end());
	
	if (isMastodonFile) {
		inflateFile(Image_Vec);
		if (Image_Vec.empty()) {
			std::cerr << "\nFile Size Error: File is zero bytes. Probable failure inflating file.\n\n";
			return 1;
		}
		const uint32_t PDV_ICCP_SIG_POS = static_cast<uint32_t>(std::search(Image_Vec.begin(), Image_Vec.end(), std::begin(PDV_SIG), std::end(PDV_SIG)) - Image_Vec.begin());
		
		if (PDV_ICCP_SIG_POS == Image_Vec.size()) {
			std::cerr << "\nImage File Error: This is not a pdvrdt image.\n\n";
			return 1;
		}
	}

	if (IMAGE_FILE_SIZE > LARGE_FILE_SIZE) {
		std::cout << "\nPlease wait. Larger files will take longer to process.\n";
	}

	const std::string DECRYPTED_FILENAME = decryptFile(Image_Vec, isMastodonFile);

	const uint32_t INFLATED_FILE_SIZE = inflateFile(Image_Vec);

	if (Image_Vec.empty()) {
		
		std::fstream file(IMAGE_FILENAME, std::ios::in | std::ios::out | std::ios::binary); 
   		 
	    	file.seekg(-1, std::ios::end);
	    	std::streampos byte_pos = file.tellg();

	    	file.read(reinterpret_cast<char*>(&byte), sizeof(byte));

		if (byte == 0x82) {
			byte = 0;
		} else {
    			byte++;
		}

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

	std::reverse(Image_Vec.begin(), Image_Vec.end());
		
	std::ofstream file_ofs(DECRYPTED_FILENAME, std::ios::binary);

	if (!file_ofs) {
		std::cerr << "\nWrite File Error: Unable to write to file.\n\n";
		return 1;
	}

	file_ofs.write(reinterpret_cast<const char*>(Image_Vec.data()), INFLATED_FILE_SIZE);

	std::vector<uint8_t>().swap(Image_Vec);

	std::cout << "\nExtracted hidden file: " << DECRYPTED_FILENAME << " (" << INFLATED_FILE_SIZE << " bytes).\n\nComplete! Please check your file.\n\n";

	return 0;
}
