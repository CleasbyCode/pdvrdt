uint_fast8_t pdvIn(const std::string& IMAGE_FILENAME, std::string& data_filename, bool isMastodonOption, bool isRedditOption) {
	
	constexpr uint_fast32_t
		MAX_FILE_SIZE = 1094713344, 
		MAX_FILE_SIZE_REDDIT = 19922944, 
		MAX_FILE_SIZE_MASTODON = 16777216,
		LARGE_FILE_SIZE = 104857600;

	constexpr uint_fast8_t PNG_MIN_FILE_SIZE = 68;

	const size_t 
		IMAGE_FILE_SIZE = std::filesystem::file_size(IMAGE_FILENAME),
		DATA_FILE_SIZE = std::filesystem::file_size(data_filename),
		COMBINED_FILE_SIZE = DATA_FILE_SIZE + IMAGE_FILE_SIZE;

	if (COMBINED_FILE_SIZE > MAX_FILE_SIZE
   		 || (isMastodonOption && COMBINED_FILE_SIZE > MAX_FILE_SIZE_MASTODON)
		 || (isRedditOption && COMBINED_FILE_SIZE > MAX_FILE_SIZE_REDDIT)
		 || PNG_MIN_FILE_SIZE > IMAGE_FILE_SIZE) {

	   std::cerr << "\nFile Size Error: " 
		<< (PNG_MIN_FILE_SIZE > IMAGE_FILE_SIZE
        		? "Size of image is too small to be a valid PNG image"
	        	: "Combined size of image and data file exceeds the maximum limit of "
        	    	+ std::string(isMastodonOption 
                		? "16MB"
	                	: (isRedditOption 
        	            		? "19MB"
                	    		: "1GB"))) 
		<< ".\n\n";
    		return 1;
	}

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

	std::vector<uint_fast8_t>Image_Vec((std::istreambuf_iterator<char>(image_file_ifs)), std::istreambuf_iterator<char>());

	constexpr uint_fast8_t
		PNG_SIG[] 	{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A },
		PNG_IEND_SIG[]	{ 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

	if (!std::equal(std::begin(PNG_SIG), std::end(PNG_SIG), std::begin(Image_Vec)) 
		|| !std::equal(std::begin(PNG_IEND_SIG), std::end(PNG_IEND_SIG), std::end(Image_Vec) - 8)) {
        		std::cerr << "\nImage File Error: Signature check failure. This file is not a valid PNG image.\n\n";
		return 1;
    	}

	if (!eraseChunks(Image_Vec)) {
		return 1;
	};

	const uint_fast8_t LAST_SLASH_POS = static_cast<uint_fast8_t>(data_filename.find_last_of("\\/"));

	if (LAST_SLASH_POS <= data_filename.length()) {
		const std::string_view NO_SLASH_NAME(data_filename.c_str() + (LAST_SLASH_POS + 1), data_filename.length() - (LAST_SLASH_POS + 1));
		data_filename = NO_SLASH_NAME;
	}

	constexpr uint_fast8_t MAX_FILENAME_LENGTH = 23;

	const uint_fast8_t DATA_FILENAME_LENGTH = static_cast<uint_fast8_t>(data_filename.length());

	if (DATA_FILENAME_LENGTH > DATA_FILE_SIZE || DATA_FILENAME_LENGTH > MAX_FILENAME_LENGTH) {
    		std::cerr << "\nData File Error: " 
              		<< (DATA_FILENAME_LENGTH > MAX_FILENAME_LENGTH 
                	  ? "Length of data filename is too long.\n\nFor compatibility requirements, length of data filename must be under 24 characters"
                  	  : "Size of data file is too small.\n\nFor compatibility requirements, data file size must be greater than the length of the filename")
              		<< ".\n\n";
		return 1;
	}

	if (DATA_FILE_SIZE > LARGE_FILE_SIZE) {
		std::cout << "\nPlease wait. Larger files will take longer to process.\n";
	}

	std::vector<uint_fast8_t>File_Vec((std::istreambuf_iterator<char>(data_file_ifs)), std::istreambuf_iterator<char>());

	std::reverse(File_Vec.begin(), File_Vec.end());

	if (isRedditOption) {

		constexpr uint_fast8_t IDAT_REDDIT_CRC_BYTES[] { 0xA3, 0x1A, 0x50, 0xFA };

		std::vector<uint_fast8_t>Idat_Reddit_Vec = { 0x00, 0x08, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54 };
		std::fill_n(std::back_inserter(Idat_Reddit_Vec), 0x80000, 0);

		Idat_Reddit_Vec.insert(Idat_Reddit_Vec.end(), std::begin(IDAT_REDDIT_CRC_BYTES), std::end(IDAT_REDDIT_CRC_BYTES));

		Image_Vec.insert(Image_Vec.end() - 12, Idat_Reddit_Vec.begin(), Idat_Reddit_Vec.end());
	}
				 
	uint_fast32_t PROFILE_DATA_VEC_SIZE = encryptFile(Profile_Data_Vec, File_Vec, data_filename);

	uint_fast8_t
		profile_data_vec_size_index{},
		chunk_size_index{},
		bits = 32;

	valueUpdater(Profile_Data_Vec, profile_data_vec_size_index, PROFILE_DATA_VEC_SIZE, bits);
	
	const uint_fast32_t PROFILE_DATA_VEC_DEFLATE_SIZE = deflateFile(Profile_Data_Vec);
	
	if (!PROFILE_DATA_VEC_DEFLATE_SIZE) {
		std::cerr << "\nFile Size Error: File is zero bytes. Compression failure.\n\n";
		return 1;
	}

	constexpr uint_fast16_t TWITTER_ICCP_SIZE_LIMIT = 9700;

	// Even if Mastodon option NOT selected, we default to it anyway if Reddit option false & data file size is under ~10KB, as this small size is compatible with X/Twitter.
	// The small data file is then stored in the ICCP chunk instead of the last IDAT chunk.
	if (TWITTER_ICCP_SIZE_LIMIT >= PROFILE_DATA_VEC_DEFLATE_SIZE && !isRedditOption) {
		isMastodonOption = true;
	}

	constexpr uint_fast8_t CHUNK_START_INDEX = 4;

	const uint_fast32_t
		IMAGE_VEC_SIZE = static_cast<uint_fast32_t>(Image_Vec.size()),
		CHUNK_SIZE = isMastodonOption ? PROFILE_DATA_VEC_DEFLATE_SIZE + 9 : PROFILE_DATA_VEC_DEFLATE_SIZE + 5,
		CHUNK_INSERT_INDEX = isMastodonOption ? 0x21 : IMAGE_VEC_SIZE - 12;

	const uint_fast8_t PROFILE_DATA_INSERT_INDEX = isMastodonOption ? 0x0D : 9;

	if (isMastodonOption) {
		std::vector<uint_fast8_t>Iccp_Chunk_Vec = { 0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

		Iccp_Chunk_Vec.insert((Iccp_Chunk_Vec.begin() + PROFILE_DATA_INSERT_INDEX), Profile_Data_Vec.begin(), Profile_Data_Vec.end());

		valueUpdater(Iccp_Chunk_Vec, chunk_size_index, PROFILE_DATA_VEC_DEFLATE_SIZE + 5, bits);

		const uint_fast32_t ICCP_CHUNK_CRC = crcUpdate(&Iccp_Chunk_Vec[CHUNK_START_INDEX], CHUNK_SIZE);
		uint_fast32_t iccp_crc_insert_index = CHUNK_START_INDEX + CHUNK_SIZE;

		valueUpdater(Iccp_Chunk_Vec, iccp_crc_insert_index, ICCP_CHUNK_CRC, bits);
		Image_Vec.insert((Image_Vec.begin() + CHUNK_INSERT_INDEX), Iccp_Chunk_Vec.begin(), Iccp_Chunk_Vec.end());
	} else {
	     	std::vector<uint_fast8_t>Idat_Chunk_Vec = { 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54, 0x5E, 0x00, 0x00, 0x00, 0x00 };

	     	Idat_Chunk_Vec.insert(Idat_Chunk_Vec.begin() + PROFILE_DATA_INSERT_INDEX, Profile_Data_Vec.begin(), Profile_Data_Vec.end());
		
		valueUpdater(Idat_Chunk_Vec, chunk_size_index, PROFILE_DATA_VEC_DEFLATE_SIZE + 1, bits);

		const uint_fast32_t IDAT_CHUNK_CRC = crcUpdate(&Idat_Chunk_Vec[CHUNK_START_INDEX], CHUNK_SIZE);
		uint_fast32_t idat_crc_insert_index = CHUNK_START_INDEX + CHUNK_SIZE;

		valueUpdater(Idat_Chunk_Vec, idat_crc_insert_index, IDAT_CHUNK_CRC, bits);
		Image_Vec.insert((Image_Vec.begin() + CHUNK_INSERT_INDEX), Idat_Chunk_Vec.begin(), Idat_Chunk_Vec.end());
	}
	
	const uint_fast32_t 
		EMBEDDED_IMAGE_FILE_SIZE = static_cast<uint_fast32_t>(Image_Vec.size()),
		ADDITIONAL_ALLOWANCE_SIZE = 2097152;

	if ((isMastodonOption && EMBEDDED_IMAGE_FILE_SIZE > MAX_FILE_SIZE_MASTODON) ||
    		(isRedditOption && EMBEDDED_IMAGE_FILE_SIZE > MAX_FILE_SIZE_REDDIT) ||
    			(EMBEDDED_IMAGE_FILE_SIZE > (MAX_FILE_SIZE + ADDITIONAL_ALLOWANCE_SIZE))) {
    
    		std::cout << "\nImage Size Error: The file embedded image exceeds the maximum size of " 
              		<< (isMastodonOption ? "16MB." 
              			: (isRedditOption ? "19MB." : "1GB.")) 
              		<< "\n\nYour data file has not been compressible and has had the opposite effect of making the file larger than the original. "
              		<< "Perhaps try again with a smaller cover image.\n\n";
    		return 1;
	}
				 
	srand((unsigned)time(NULL));
	const std::string 
		TIME_VALUE = std::to_string(rand()),
		EMBEDDED_IMAGE_FILENAME = "prdt_" + TIME_VALUE.substr(0, 5) + ".png";

	std::ofstream file_ofs(EMBEDDED_IMAGE_FILENAME, std::ios::binary);

	if (!file_ofs) {
		std::cerr << "\nWrite File Error: Unable to write to file.\n\n";
		return 1;
	}

	file_ofs.write((char*)&Image_Vec[0], EMBEDDED_IMAGE_FILE_SIZE);

	if ((isMastodonOption && PROFILE_DATA_VEC_DEFLATE_SIZE > TWITTER_ICCP_SIZE_LIMIT) || isRedditOption) {
    		const std::string PLATFORM_OPTION = isMastodonOption ? "Mastodon" : "Reddit";
    		std::cout << "\n**Important**\n\nDue to your option selection, for compatibility reasons\nyou should only post this file-embedded PNG image on " << PLATFORM_OPTION << ".\n";
	}
				 
	std::cout << "\nSaved PNG image: " + EMBEDDED_IMAGE_FILENAME + '\x20' + std::to_string(EMBEDDED_IMAGE_FILE_SIZE) + " Bytes.\n\nComplete!\n\nYou can now post your file-embedded PNG image on the relevant supported platforms.\n\n";

	return 0;
}
