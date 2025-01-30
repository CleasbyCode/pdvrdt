int pdvIn(const std::string& IMAGE_FILENAME, std::string& data_filename, ArgOption platformOption) {
	constexpr uint32_t
		COMBINED_MAX_FILE_SIZE 	= 2U * 1024U * 1024U * 1024U,	// 2GB. (image + data file)
		MAX_FILE_SIZE_REDDIT 	= 20 * 1024 * 1024, 		// 20MB. ""
		MAX_FILE_SIZE_MASTODON 	= 16 * 1024 * 1024;		// 16MB. ""

	constexpr uint8_t PNG_MIN_FILE_SIZE = 68;

	const size_t 
		IMAGE_FILE_SIZE = std::filesystem::file_size(IMAGE_FILENAME),
		DATA_FILE_SIZE = std::filesystem::file_size(data_filename),
		COMBINED_FILE_SIZE = DATA_FILE_SIZE + IMAGE_FILE_SIZE;

       const bool
		hasMastodonOption = (platformOption == ArgOption::Mastodon),
		hasRedditOption = (platformOption == ArgOption::Reddit);

	if (COMBINED_FILE_SIZE > COMBINED_MAX_FILE_SIZE
    		|| (DATA_FILE_SIZE == 0)
    		|| (hasMastodonOption && COMBINED_FILE_SIZE > MAX_FILE_SIZE_MASTODON)
    		|| (hasRedditOption && COMBINED_FILE_SIZE > MAX_FILE_SIZE_REDDIT)
    		|| PNG_MIN_FILE_SIZE > IMAGE_FILE_SIZE) {
			std::cerr << "\nFile Size Error: "
              		<< (PNG_MIN_FILE_SIZE > IMAGE_FILE_SIZE 
                  		? "Size of image is too small to be a valid PNG image" 
                  		: DATA_FILE_SIZE == 0 
                    			? "Data file is empty" 
                    		: COMBINED_FILE_SIZE > COMBINED_MAX_FILE_SIZE 
                      			? "Combined size of image and data file exceeds the maximum limit of 2GB"
                      		: (hasMastodonOption && COMBINED_FILE_SIZE > MAX_FILE_SIZE_MASTODON) 
                        		? "Combined size exceeds Mastodon's 16MB limit"
                        	: "Combined size exceeds Reddit's 19MB limit") 
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

	std::vector<uint8_t> Image_Vec;
	Image_Vec.resize(IMAGE_FILE_SIZE); 
	
	image_file_ifs.read(reinterpret_cast<char*>(Image_Vec.data()), IMAGE_FILE_SIZE);
	image_file_ifs.close();

	constexpr uint8_t
		PNG_SIG[] 	{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A },
		PNG_IEND_SIG[]	{ 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 },
		MAX_FILENAME_LENGTH = 20;

	if (!std::equal(std::begin(PNG_SIG), std::end(PNG_SIG), std::begin(Image_Vec)) || !std::equal(std::begin(PNG_IEND_SIG), std::end(PNG_IEND_SIG), std::end(Image_Vec) - 8)) {
        	std::cerr << "\nImage File Error: Signature check failure. This file is not a valid PNG image.\n\n";
		return 1;
    	}

	eraseChunks(Image_Vec, IMAGE_FILE_SIZE);
		
	std::filesystem::path filePath(data_filename);
    	data_filename = filePath.filename().string();

	const uint8_t DATA_FILENAME_LENGTH = static_cast<uint8_t>(data_filename.length());

	if (DATA_FILENAME_LENGTH > MAX_FILENAME_LENGTH) {
    		std::cerr << "\nData File Error: Length of data filename is too long.\n\nFor compatibility requirements, length of filename must not exceed 20 characters.\n\n";
		return 1;
	}

	std::vector<uint8_t>Profile_Data_Vec;

	if (hasMastodonOption) {
		Profile_Data_Vec = std::move(Mastodon_Vec);
	} else {
		Profile_Data_Vec = std::move(Default_Vec);
	}

	const uint16_t PROFILE_NAME_LENGTH_INDEX = hasMastodonOption ? 0x191: 0x00;
	Profile_Data_Vec[PROFILE_NAME_LENGTH_INDEX] = DATA_FILENAME_LENGTH;

	constexpr uint32_t LARGE_FILE_SIZE = 400 * 1024 * 1024;  // 400MB.

	if (DATA_FILE_SIZE > LARGE_FILE_SIZE) {
		std::cout << "\nPlease wait. Larger files will take longer to complete this process.\n";
	}

	std::vector<uint8_t> File_Vec;
	File_Vec.resize(DATA_FILE_SIZE); 

	data_file_ifs.read(reinterpret_cast<char*>(File_Vec.data()), DATA_FILE_SIZE);
	data_file_ifs.close();

	std::reverse(File_Vec.begin(), File_Vec.end());

	uint32_t file_vec_size = deflateFile(File_Vec, hasMastodonOption);
	
	if (!file_vec_size) {
		std::cerr << "\nFile Size Error: File is zero bytes. Probable compression failure.\n\n";
		return 1;
	}

	const uint32_t PIN = encryptFile(Profile_Data_Vec, File_Vec, file_vec_size, data_filename, hasMastodonOption);

	if (hasRedditOption) {
		constexpr uint8_t IDAT_REDDIT_CRC_BYTES[] { 0xA3, 0x1A, 0x50, 0xFA };

		std::vector<uint8_t>Idat_Reddit_Vec = { 0x00, 0x08, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54 };
		std::fill_n(std::back_inserter(Idat_Reddit_Vec), 0x80000, 0);

		Idat_Reddit_Vec.insert(Idat_Reddit_Vec.end(), std::begin(IDAT_REDDIT_CRC_BYTES), std::end(IDAT_REDDIT_CRC_BYTES));

		Image_Vec.insert(Image_Vec.end() - 12, Idat_Reddit_Vec.begin(), Idat_Reddit_Vec.end());
	}

	uint32_t mastodon_deflate_size = 0;

	if (hasMastodonOption) {
		// Compresss the data chunk again, this time including the color profile. Requirement for iCCP chunk/Mastodon.
		mastodon_deflate_size = deflateFile(Profile_Data_Vec, hasMastodonOption);
	}

	constexpr uint8_t CHUNK_START_INDEX = 4;

	uint8_t
		chunk_size_index = 0,
		bits = 32;

	const uint32_t
		PROFILE_DATA_VEC_DEFLATE_SIZE = static_cast<uint32_t>(Profile_Data_Vec.size()),
		IMAGE_VEC_SIZE = static_cast<uint32_t>(Image_Vec.size()),
		CHUNK_SIZE = hasMastodonOption ? mastodon_deflate_size + 9 : PROFILE_DATA_VEC_DEFLATE_SIZE + 3,
		CHUNK_INDEX = hasMastodonOption ? 0x21 : IMAGE_VEC_SIZE - 12;

	const uint8_t PROFILE_DATA_INDEX = hasMastodonOption ? 0x0D : 0x0B;

	if (hasMastodonOption) {
		std::vector<uint8_t>Iccp_Chunk_Vec = { 0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
		Iccp_Chunk_Vec.reserve(Iccp_Chunk_Vec.size() + CHUNK_SIZE);

		Iccp_Chunk_Vec.insert((Iccp_Chunk_Vec.begin() + PROFILE_DATA_INDEX), Profile_Data_Vec.begin(), Profile_Data_Vec.end());
		valueUpdater(Iccp_Chunk_Vec, chunk_size_index, mastodon_deflate_size + 5, bits);

		const uint32_t ICCP_CHUNK_CRC = crcUpdate(&Iccp_Chunk_Vec[CHUNK_START_INDEX], CHUNK_SIZE);
		uint32_t iccp_crc_index = CHUNK_START_INDEX + CHUNK_SIZE;

		valueUpdater(Iccp_Chunk_Vec, iccp_crc_index, ICCP_CHUNK_CRC, bits);
		Image_Vec.insert((Image_Vec.begin() + CHUNK_INDEX), Iccp_Chunk_Vec.begin(), Iccp_Chunk_Vec.end());
	} else {
	     	std::vector<uint8_t>Idat_Chunk_Vec = { 0x00, 0x00, 0x00, 0x00, 0x49, 0x44, 0x41, 0x54, 0x78, 0x5E, 0x5C, 0x00, 0x00, 0x00, 0x00 };
		Idat_Chunk_Vec.reserve(Idat_Chunk_Vec.size() + CHUNK_SIZE);

	     	Idat_Chunk_Vec.insert(Idat_Chunk_Vec.begin() + PROFILE_DATA_INDEX, Profile_Data_Vec.begin(), Profile_Data_Vec.end());		
		valueUpdater(Idat_Chunk_Vec, chunk_size_index, CHUNK_SIZE, bits);

		const uint32_t IDAT_CHUNK_CRC = crcUpdate(&Idat_Chunk_Vec[CHUNK_START_INDEX], CHUNK_SIZE + 4);
		uint32_t idat_crc_index = CHUNK_SIZE + 8;

		valueUpdater(Idat_Chunk_Vec, idat_crc_index, IDAT_CHUNK_CRC, bits);
		Image_Vec.insert((Image_Vec.begin() + CHUNK_INDEX), Idat_Chunk_Vec.begin(), Idat_Chunk_Vec.end());
	}
	
	std::vector<uint8_t>().swap(Profile_Data_Vec);

	if (!writeFile(Image_Vec)) {
		return 1;
	}

	std::cout << "\nRecovery PIN: [***" << PIN << "***]\n\nImportant: Please remember to keep your PIN safe, so that you can extract the hidden file.\n";
			 
	if (platformOption != ArgOption::Default) {
    		const std::string PLATFORM_OPTION = hasMastodonOption ? "Mastodon" : "Reddit";
    		std::cout << "\n**Important**\n\nDue to your option selection, for compatibility reasons\nyou should only post this file-embedded PNG image on " << PLATFORM_OPTION << ".\n\nComplete!\n\n";
	} else {
	    std::cout << "\nComplete!\n\n";
	}			 
	return 0;
}
