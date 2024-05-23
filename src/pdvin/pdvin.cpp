void startPdv(std::string& image_file_name, std::string& data_file_name, bool isMastodonOption, bool isRedditOption) {
	
	std::ifstream 
		image_file_ifs(image_file_name, std::ios::binary),
		data_file_ifs(data_file_name, std::ios::binary);

	if (!image_file_ifs || !data_file_ifs ) {
		std::cerr << "\nRead File Error: " << (!image_file_ifs ? "Unable to read image file" : "Unable to read data file") << ".\n\n";
		std::exit(EXIT_FAILURE);
	}

	image_file_ifs.seekg(0, image_file_ifs.end);
	data_file_ifs.seekg(0, data_file_ifs.end);
	
	size_t
		tmp_image_file_size = image_file_ifs.tellg(),
		tmp_data_file_size = data_file_ifs.tellg();

	image_file_ifs.seekg(0, image_file_ifs.beg);
	data_file_ifs.seekg(0, data_file_ifs.beg);

	constexpr uint_fast32_t
		MAX_FILE_SIZE = 209715200, 
		MAX_FILE_SIZE_REDDIT = 19922944, 
		MAX_FILE_SIZE_MASTODON = 16777216,
		LARGE_FILE_SIZE = 52428800;

	constexpr uint_fast8_t PNG_MIN_SIZE = 68;

	if (tmp_image_file_size > MAX_FILE_SIZE
		|| isMastodonOption && tmp_image_file_size > MAX_FILE_SIZE_MASTODON
		|| isRedditOption && tmp_image_file_size > MAX_FILE_SIZE_REDDIT
		|| PNG_MIN_SIZE > tmp_image_file_size) {

		std::cerr << "\nImage File Error: " << (PNG_MIN_SIZE > tmp_image_file_size ? "Size of image is too small to be a valid PNG image"
			: "Size of image exceeds the maximum limit of " + (isMastodonOption ? std::to_string(MAX_FILE_SIZE_MASTODON) + " Bytes"
				: (isRedditOption ? std::to_string(MAX_FILE_SIZE_REDDIT)
					: std::to_string(MAX_FILE_SIZE)) + " Bytes")) << ".\n\n";
		std::exit(EXIT_FAILURE);
	}

	if (tmp_data_file_size > LARGE_FILE_SIZE) {
		std::cout << "\nPlease Wait. Large files will take longer to process.\n";
	}

	std::vector<uint_fast8_t>Image_Vec((std::istreambuf_iterator<char>(image_file_ifs)), std::istreambuf_iterator<char>());

	uint_fast32_t image_file_size = static_cast<uint_fast32_t>(Image_Vec.size());

	constexpr uint_fast8_t
		PNG_SIG[8] 	{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A },
		PNG_IEND_SIG[8]	{ 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

	if (!std::equal(std::begin(PNG_SIG), std::end(PNG_SIG), std::begin(Image_Vec)) || !std::equal(std::begin(PNG_IEND_SIG), std::end(PNG_IEND_SIG), std::end(Image_Vec) - 8)) {
        		std::cerr << "\nImage File Error: Signature check failure. This file is not a valid PNG image.\n\n";
			std::exit(EXIT_FAILURE);
    	}

	image_file_size = eraseChunks(Image_Vec, image_file_size);

	const uint_fast32_t LAST_SLASH_POS = static_cast<uint_fast32_t>(data_file_name.find_last_of("\\/"));

	if (LAST_SLASH_POS <= data_file_name.length()) {
		const std::string_view NO_SLASH_NAME(data_file_name.c_str() + (LAST_SLASH_POS + 1), data_file_name.length() - (LAST_SLASH_POS + 1));
		data_file_name = NO_SLASH_NAME;
	}

	constexpr uint_fast8_t MAX_FILENAME_LENGTH = 23;

	const uint_fast32_t FILE_NAME_LENGTH = static_cast<uint_fast32_t>(data_file_name.length());

	if (tmp_data_file_size > MAX_FILE_SIZE
		|| isMastodonOption && tmp_data_file_size > MAX_FILE_SIZE_MASTODON
		|| isRedditOption && tmp_data_file_size > MAX_FILE_SIZE_REDDIT
		|| FILE_NAME_LENGTH > tmp_data_file_size
		|| FILE_NAME_LENGTH > MAX_FILENAME_LENGTH) {
		std::cerr << "\nData File Error: " << (FILE_NAME_LENGTH > MAX_FILENAME_LENGTH ? "Length of file name is too long.\n\nFor compatibility requirements, length of file name must be under 24 characters"
			: (FILE_NAME_LENGTH > tmp_data_file_size ? "Size of file is too small.\n\nFor compatibility requirements, file size must be greater than the length of the file name"
				: "Size of file exceeds the maximum limit of " + (isMastodonOption ? std::to_string(MAX_FILE_SIZE_MASTODON) + " Bytes"
					: (isRedditOption ? std::to_string(MAX_FILE_SIZE_REDDIT)
						: std::to_string(MAX_FILE_SIZE)) + " Bytes"))) << ".\n\n";
		std::exit(EXIT_FAILURE);
	}

	std::vector<uint_fast8_t>File_Vec((std::istreambuf_iterator<char>(data_file_ifs)), std::istreambuf_iterator<char>());

	uint_fast32_t data_file_size = static_cast<uint_fast32_t>(File_Vec.size());

	std::reverse(File_Vec.begin(), File_Vec.end());

	if (image_file_size + data_file_size > MAX_FILE_SIZE
		|| isMastodonOption && image_file_size + data_file_size > MAX_FILE_SIZE_MASTODON
		|| isRedditOption && image_file_size + data_file_size > MAX_FILE_SIZE_REDDIT) {

		std::cerr << "\nFile Size Error: The combined file size of the PNG image & data file, must not exceed "
			<< (isMastodonOption ? std::to_string(MAX_FILE_SIZE_MASTODON)
				: (isRedditOption ? std::to_string(MAX_FILE_SIZE_REDDIT)
					: std::to_string(MAX_FILE_SIZE))) << " Bytes.\n\n";
		std::exit(EXIT_FAILURE);
	}
	
	std::vector<uint_fast8_t>Profile_Data_Vec = {
		0x00, 0x00, 0x00, 0x00, 0x6c, 0x63, 0x6d, 0x73, 0x02, 0x10, 0x00, 0x00, 0x6D, 0x6E, 0x74,
		0x72, 0x52, 0x47, 0x42, 0x20, 0x58, 0x59, 0x5A, 0x20, 0x07, 0xE2, 0x00, 0x03, 0x00, 0x14,
		0x00, 0x09, 0x00, 0x0E, 0x00, 0x1D, 0x61, 0x63, 0x73, 0x70, 0x4D, 0x53, 0x46, 0x54, 0x00,
		0x00, 0x00, 0x00, 0x73, 0x61, 0x77, 0x73, 0x63, 0x74, 0x72, 0x6C, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF6, 0xD6, 0x00, 0x01, 0x00,
		0x00, 0x00, 0x00, 0xD3, 0x2D, 0x68, 0x61, 0x6E, 0x64, 0xEB, 0x77, 0x1F, 0x3C, 0xAA, 0x53,
		0x51, 0x02, 0xE9, 0x3E, 0x28, 0x6C, 0x91, 0x46, 0xAE, 0x57, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x64, 0x65, 0x73,
		0x63, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x1C, 0x77, 0x74, 0x70, 0x74, 0x00, 0x00,
		0x01, 0x0C, 0x00, 0x00, 0x00, 0x14, 0x72, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x20, 0x00,
		0x00, 0x00, 0x14, 0x67, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x34, 0x00, 0x00, 0x00, 0x14,
		0x62, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x48, 0x00, 0x00, 0x00, 0x14, 0x72, 0x54, 0x52,
		0x43, 0x00, 0x00, 0x01, 0x5C, 0x00, 0x00, 0x00, 0x34, 0x67, 0x54, 0x52, 0x43, 0x00, 0x00,
		0x01, 0x5C, 0x00, 0x00, 0x00, 0x34, 0x62, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0x5C, 0x00,
		0x00, 0x00, 0x34, 0x63, 0x70, 0x72, 0x74, 0x00, 0x00, 0x01, 0x90, 0x00, 0x00, 0x00, 0x01,
		0x64, 0x65, 0x73, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x6E, 0x52, 0x47,
		0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0x59,
		0x5A, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x54, 0x00, 0x01, 0x00, 0x00, 0x00,
		0x01, 0x16, 0xC9, 0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6F, 0xA0,
		0x00, 0x00, 0x38, 0xF2, 0x00, 0x00, 0x03, 0x8F, 0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x62, 0x96, 0x00, 0x00, 0xB7, 0x89, 0x00, 0x00, 0x18, 0xDA, 0x58, 0x59,
		0x5A, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0xA0, 0x00, 0x00, 0x0F, 0x85, 0x00,
		0x00, 0xB6, 0xC4, 0x63, 0x75, 0x72, 0x76, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14,
		0x00, 0x00, 0x01, 0x07, 0x02, 0xB5, 0x05, 0x6B, 0x09, 0x36, 0x0E, 0x50, 0x14, 0xB1, 0x1C,
		0x80, 0x25, 0xC8, 0x30, 0xA1, 0x3D, 0x19, 0x4B, 0x40, 0x5B, 0x27, 0x6C, 0xDB, 0x80, 0x6B,
		0x95, 0xE3, 0xAD, 0x50, 0xC6, 0xC2, 0xE2, 0x31, 0xFF, 0xFF, 0x00, 0x50, 0x44, 0x56
	};

	Profile_Data_Vec.reserve(data_file_size);

	if (isRedditOption) {

		constexpr uint_fast8_t IDAT_REDDIT_CRC_BYTES[4] { 0xA3, 0x1A, 0x50, 0xFA };

		std::vector<uint_fast8_t>Idat_Reddit_Vec = 	{ 0x00, 0x08, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54 };

		std::fill_n(std::back_inserter(Idat_Reddit_Vec), 0x80000, 0);

		Idat_Reddit_Vec.insert(Idat_Reddit_Vec.end(), &IDAT_REDDIT_CRC_BYTES[0], &IDAT_REDDIT_CRC_BYTES[4]);
		Image_Vec.insert(Image_Vec.end() - 12, Idat_Reddit_Vec.begin(), Idat_Reddit_Vec.end());
	}

	encryptFile(Profile_Data_Vec, File_Vec, data_file_name);

	uint_fast8_t
		profile_data_vec_size_index{},
		chunk_size_index{},
		bits = 32;

	valueUpdater(Profile_Data_Vec, profile_data_vec_size_index, static_cast<uint_fast32_t>(Profile_Data_Vec.size()), bits);
	
	deflateFile(Profile_Data_Vec);
	
	// Even if Mastodon option not selected, we default to it anyway if data file size is under ~10KB, as this small size is supported under X/Twitter.
        if (Profile_Data_Vec.size() <= 9700 && !isRedditOption) isMastodonOption = true;

	constexpr uint_fast8_t CHUNK_START_INDEX = 4;

	const uint_fast32_t
		PROFILE_DEFLATE_SIZE = static_cast<uint_fast32_t>(Profile_Data_Vec.size()),
		CHUNK_SIZE = isMastodonOption ? PROFILE_DEFLATE_SIZE + 9 : PROFILE_DEFLATE_SIZE + 5,
		CHUNK_INSERT_INDEX = isMastodonOption ? 0x21 : static_cast<uint_fast32_t>(Image_Vec.size() - 12);

	const uint_fast8_t PROFILE_DATA_INSERT_INDEX = isMastodonOption ? 0x0D : 9;

	uint_fast32_t
		buf_index{},
		initialize_crc_value = 0xffffffffL;

	if (isMastodonOption) {

		std::vector<uint_fast8_t>Iccp_Chunk_Vec = { 0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
				
		Iccp_Chunk_Vec.reserve(Profile_Data_Vec.size());
	
		Iccp_Chunk_Vec.insert((Iccp_Chunk_Vec.begin() + PROFILE_DATA_INSERT_INDEX), Profile_Data_Vec.begin(), Profile_Data_Vec.end());
				
		Image_Vec.reserve(Iccp_Chunk_Vec.size());

		valueUpdater(Iccp_Chunk_Vec, chunk_size_index, PROFILE_DEFLATE_SIZE + 5, bits);

		const uint_fast32_t ICCP_CHUNK_CRC = crcUpdate(&Iccp_Chunk_Vec[CHUNK_START_INDEX], CHUNK_SIZE, buf_index, initialize_crc_value);

		uint_fast32_t iccp_crc_insert_index = CHUNK_START_INDEX + CHUNK_SIZE;

		valueUpdater(Iccp_Chunk_Vec, iccp_crc_insert_index, ICCP_CHUNK_CRC, bits);

		Image_Vec.insert((Image_Vec.begin() + CHUNK_INSERT_INDEX), Iccp_Chunk_Vec.begin(), Iccp_Chunk_Vec.end());

	}
	else {

		std::vector<uint_fast8_t>Idat_Chunk_Vec = { 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54, 0x5E, 0x00, 0x00, 0x00, 0x00 };

		Idat_Chunk_Vec.reserve(Profile_Data_Vec.size());

		Idat_Chunk_Vec.insert(Idat_Chunk_Vec.begin() + PROFILE_DATA_INSERT_INDEX, Profile_Data_Vec.begin(), Profile_Data_Vec.end());
		
		Image_Vec.reserve(Idat_Chunk_Vec.size());

		valueUpdater(Idat_Chunk_Vec, chunk_size_index, PROFILE_DEFLATE_SIZE + 1, bits);

		const uint_fast32_t IDAT_CHUNK_CRC = crcUpdate(&Idat_Chunk_Vec[CHUNK_START_INDEX], CHUNK_SIZE, buf_index, initialize_crc_value);

		uint_fast32_t idat_crc_insert_index = CHUNK_START_INDEX + CHUNK_SIZE;

		valueUpdater(Idat_Chunk_Vec, idat_crc_insert_index, IDAT_CHUNK_CRC, bits);

		Image_Vec.insert((Image_Vec.begin() + CHUNK_INSERT_INDEX), Idat_Chunk_Vec.begin(), Idat_Chunk_Vec.end());
	}

	if (isMastodonOption && Image_Vec.size() > MAX_FILE_SIZE_MASTODON || isMastodonOption && Image_Vec.size() > MAX_FILE_SIZE_REDDIT || Image_Vec.size() > MAX_FILE_SIZE) {
		std::cout << "\nImage Size Error: The file embedded image exceeds the maximum size of " << (isMastodonOption ? "16MB (Mastodon)."
			: (isRedditOption ? "19MB (Reddit)." : "200MB.")) << "\n\nYour data file has not been compressible and has had the opposite effect of making the file larger than the original. Perhaps try again with a smaller cover image.\n\n";
		std::exit(EXIT_FAILURE);
	}

	srand((unsigned)time(NULL));

	const std::string TIME_VALUE = std::to_string(rand());

	data_file_name = "prdt_" + TIME_VALUE.substr(0, 5) + ".png";

	std::ofstream file_ofs(data_file_name, std::ios::binary);

	if (!file_ofs) {
		std::cerr << "\nWrite File Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}

	image_file_size = static_cast<uint_fast32_t>(Image_Vec.size());

	file_ofs.write((char*)&Image_Vec[0], Image_Vec.size());

	if (isMastodonOption && PROFILE_DEFLATE_SIZE > 9700 || isRedditOption) {
		std::string option = isMastodonOption ? "Mastodon" : "Reddit";
		std::cout << "\n**Warning**\n\nDue to your option selection, for compatibility reasons\nyou should only post this file-embedded PNG image on " + option + ".\n";
	}
	std::cout << "\nSaved PNG image: " + data_file_name + '\x20' + std::to_string(image_file_size) + " Bytes.\n\nComplete!\n\nYou can now post your file-embedded PNG image on the relevant supported platforms.\n\n";
}