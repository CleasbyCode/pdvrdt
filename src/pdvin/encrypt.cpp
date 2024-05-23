void encryptFile(std::vector<uint_fast8_t>&Profile_Data_Vec, std::vector<uint_fast8_t>&File_Vec, std::string& FILE_NAME) {

	std::string encrypted_file_name;

	const uint_fast8_t FILE_NAME_LENGTH = static_cast<uint_fast8_t>(FILE_NAME.length());

	constexpr uint_fast8_t 	
		XOR_KEY_LENGTH = 6,
		PROFILE_NAME_INDEX = 0x65;

	uint_fast32_t
		file_size = static_cast<uint_fast32_t>(File_Vec.size()),
		index_pos{};

	uint_fast8_t
		xor_key[XOR_KEY_LENGTH] { 0xFC, 0xD8, 0xF9, 0xE2, 0x9F, 0x7E },
		xor_key_pos{},
		name_key_pos{};

	while (file_size > index_pos) {

		std::reverse(std::begin(xor_key), std::end(xor_key));
		
		if (index_pos >= FILE_NAME_LENGTH) {
			name_key_pos = name_key_pos > FILE_NAME_LENGTH ? 0 : name_key_pos;
		}
		else {
			xor_key_pos = xor_key_pos > XOR_KEY_LENGTH ? 0 : xor_key_pos;
			encrypted_file_name += FILE_NAME[index_pos] ^ xor_key[xor_key_pos++];
		}
		Profile_Data_Vec.emplace_back(File_Vec[index_pos++] ^ encrypted_file_name[name_key_pos++]);
	}

	Profile_Data_Vec[PROFILE_NAME_INDEX - 1] = static_cast<uint_fast8_t>(FILE_NAME_LENGTH);

	std::copy(encrypted_file_name.begin(), encrypted_file_name.end(), Profile_Data_Vec.begin() + PROFILE_NAME_INDEX);
}