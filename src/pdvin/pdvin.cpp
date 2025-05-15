uint8_t pdvIn(const std::string& IMAGE_FILENAME, std::string& data_filename, ArgOption platform, bool isCompressedFile) {
	std::ifstream 
		image_file_ifs(IMAGE_FILENAME, std::ios::binary),
		data_file_ifs(data_filename, std::ios::binary);

	if (!image_file_ifs || !data_file_ifs ) {
		std::cerr << "\nRead File Error: Unable to read " << (!image_file_ifs 
			? "image file" 
			: "data file") 
		<< ".\n\n";
		return 1;
	}

	const uintmax_t 
		IMAGE_FILE_SIZE = std::filesystem::file_size(IMAGE_FILENAME),
		DATA_FILE_SIZE = std::filesystem::file_size(data_filename);
	
	std::vector<uint8_t> image_vec(IMAGE_FILE_SIZE); 
	
	image_file_ifs.read(reinterpret_cast<char*>(image_vec.data()), IMAGE_FILE_SIZE);
	image_file_ifs.close();

	constexpr uint8_t SIG_LENGTH = 8;

	constexpr std::array<uint8_t, SIG_LENGTH>
		PNG_SIG 	{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A },
		PNG_IEND_SIG	{ 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

	constexpr uint8_t MAX_FILENAME_LENGTH = 20;

	if (!std::equal(PNG_SIG.begin(), PNG_SIG.end(), image_vec.begin()) || !std::equal(PNG_IEND_SIG.begin(), PNG_IEND_SIG.end(), image_vec.end() - 8)) {
        		std::cerr << "\nImage File Error: Signature check failure. Not a valid PNG image.\n\n";
			return 1;
    	}

	copyEssentialChunks(image_vec, IMAGE_FILE_SIZE);
		
	std::filesystem::path file_path(data_filename);
    	data_filename = file_path.filename().string();

	const uint8_t DATA_FILENAME_LENGTH = static_cast<uint8_t>(data_filename.size());

	if (DATA_FILENAME_LENGTH > MAX_FILENAME_LENGTH) {
    		std::cerr << "\nData File Error: Length of data filename is too long.\n\nFor compatibility requirements, length of filename must not exceed 20 characters.\n\n";
		return 1;
	}

	std::vector<uint8_t>profile_vec;
	profile_vec = std::move(default_vec);

	const bool
		hasMastodonOption = (platform == ArgOption::Mastodon),
		hasRedditOption = (platform == ArgOption::Reddit);

	if (hasMastodonOption) {
		profile_vec = std::move(mastodon_vec);
	} 

	const uint16_t PROFILE_NAME_LENGTH_INDEX = hasMastodonOption ? 0x191 : 0x00;
	profile_vec[PROFILE_NAME_LENGTH_INDEX] = DATA_FILENAME_LENGTH;

	constexpr uint32_t LARGE_FILE_SIZE = 300 * 1024 * 1024; 

	if (DATA_FILE_SIZE > LARGE_FILE_SIZE) {
		std::cout << "\nPlease wait. Larger files will take longer to complete this process.\n";
	}

	std::vector<uint8_t> data_file_vec(DATA_FILE_SIZE);

	data_file_ifs.read(reinterpret_cast<char*>(data_file_vec.data()), DATA_FILE_SIZE);
	data_file_ifs.close(); 

	// First, compress the data file.
	deflateFile(data_file_vec, hasMastodonOption, isCompressedFile);

	if (data_file_vec.empty()) {
		std::cerr << "\nFile Size Error: File is zero bytes. Probable compression failure.\n\n";
		return 1;
	}

	const uint64_t PIN = encryptFile(profile_vec, data_file_vec, data_filename, hasMastodonOption);

	constexpr uint8_t PNG_END_BYTES_LENGTH = 12;

	if (hasRedditOption) {
		constexpr uint8_t CRC_LENGTH = 4;

		constexpr std::array<uint8_t, CRC_LENGTH> IDAT_REDDIT_CRC_BYTES { 0xA3, 0x1A, 0x50, 0xFA };

		std::vector<uint8_t>reddit_vec = { 0x00, 0x08, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54 };
		std::fill_n(std::back_inserter(reddit_vec), 0x80000, 0);

		std::copy_n(IDAT_REDDIT_CRC_BYTES.begin(), CRC_LENGTH, std::back_inserter(reddit_vec));

		image_vec.insert(image_vec.end() - PNG_END_BYTES_LENGTH, reddit_vec.begin(), reddit_vec.end());
	}

	uint32_t mastodon_deflate_size = 0;

	if (hasMastodonOption) {
		// Compresss the data chunk again, this time including the icc color profile. A requirement for iCCP chunk/Mastodon.
		deflateFile(profile_vec, hasMastodonOption, isCompressedFile);
		mastodon_deflate_size = static_cast<uint32_t>(profile_vec.size());
	}

	constexpr uint8_t 
		CHUNK_START_INDEX = 0x04,
		MASTODON_SIZE_DIFF = 9,
		DEFAULT_SIZE_DIFF = 3;

	uint8_t
		chunk_size_index = 0,
		value_bit_length = 32;

	const uint32_t
		PROFILE_VEC_DEFLATE_SIZE = static_cast<uint32_t>(profile_vec.size()),
		IMAGE_VEC_SIZE = static_cast<uint32_t>(image_vec.size()),
		CHUNK_SIZE = hasMastodonOption ? mastodon_deflate_size + MASTODON_SIZE_DIFF : PROFILE_VEC_DEFLATE_SIZE + DEFAULT_SIZE_DIFF,
		CHUNK_INDEX = hasMastodonOption ? 0x21 : IMAGE_VEC_SIZE - PNG_END_BYTES_LENGTH;

	const uint8_t PROFILE_VEC_INDEX = hasMastodonOption ? 0x0D : 0x0B;

	if (hasMastodonOption) {
		constexpr uint8_t MASTODON_ICCP_CHUNK_SIZE_DIFF = 5;

		std::vector<uint8_t>iccp_vec = { 0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
		iccp_vec.reserve(iccp_vec.size() + PROFILE_VEC_DEFLATE_SIZE + CHUNK_SIZE);

		iccp_vec.insert((iccp_vec.begin() + PROFILE_VEC_INDEX), profile_vec.begin(), profile_vec.end());
		valueUpdater(iccp_vec, chunk_size_index, mastodon_deflate_size + MASTODON_ICCP_CHUNK_SIZE_DIFF, value_bit_length);

		const uint32_t ICCP_CHUNK_CRC = crcUpdate(&iccp_vec[CHUNK_START_INDEX], CHUNK_SIZE);
		uint32_t iccp_crc_index = CHUNK_START_INDEX + CHUNK_SIZE;

		valueUpdater(iccp_vec, iccp_crc_index, ICCP_CHUNK_CRC, value_bit_length);
		image_vec.insert((image_vec.begin() + CHUNK_INDEX), iccp_vec.begin(), iccp_vec.end());
	} else {
		constexpr uint8_t 
			IDAT_CHUNK_SIZE_DIFF = 4,
			IDAT_CHUNK_CRC_INDEX_DIFF = 0x08;

	     	std::vector<uint8_t>idat_vec = { 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54, 0x78, 0x5E, 0x5C, 0x00, 0x00, 0x00, 0x00 };
		idat_vec.reserve(idat_vec.size() + profile_vec.size() + CHUNK_SIZE);
		
	     	idat_vec.insert(idat_vec.begin() + PROFILE_VEC_INDEX, profile_vec.begin(), profile_vec.end());		
		valueUpdater(idat_vec, chunk_size_index, CHUNK_SIZE, value_bit_length);

		const uint32_t IDAT_CHUNK_CRC = crcUpdate(&idat_vec[CHUNK_START_INDEX], CHUNK_SIZE + IDAT_CHUNK_SIZE_DIFF);
		uint32_t idat_crc_index = CHUNK_SIZE + IDAT_CHUNK_CRC_INDEX_DIFF;

		valueUpdater(idat_vec, idat_crc_index, IDAT_CHUNK_CRC, value_bit_length);
		image_vec.insert((image_vec.begin() + CHUNK_INDEX), idat_vec.begin(), idat_vec.end());
	}
	
	std::vector<uint8_t>().swap(profile_vec);

	if (!writeFile(image_vec)) {
		return 1;
	}

	std::cout << "\nRecovery PIN: [***" << PIN << "***]\n\nImportant: Keep your PIN safe, so that you can extract the hidden file.\n";
			 
	if (platform != ArgOption::Default) {
    		const std::string PLATFORM_OPTION = hasMastodonOption ? "Mastodon" : "Reddit";
    		std::cout << '\n' << PLATFORM_OPTION << " option selected: Only post/share this file-embedded PNG image on " << PLATFORM_OPTION << ".\n\nComplete!\n\n";
	} else {
	    std::cout << "\nComplete!\n\n";
	}			 
	return 0;
}
