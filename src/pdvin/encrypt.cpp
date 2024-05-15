void encryptFile(std::vector<uchar>&Profile_Data_Vec, std::vector<uchar>&File_Vec, std::string& FILE_NAME) {

	std::string 
		xor_key = "\xFC\xD8\xF9\xE2\x9F\x7E",
		output_name;

	const uint8_t
		XOR_KEY_LENGTH = static_cast<uint8_t>(xor_key.length()),
		FILE_NAME_LENGTH = static_cast<uint8_t>(FILE_NAME.length());

	uint32_t
		file_size = static_cast<uint32_t>(File_Vec.size()),
		index_pos = 0;

	uint8_t
		xor_key_pos = 0,
		name_key_pos = 0;

	while (file_size > index_pos) {

		std::reverse(xor_key.begin(), xor_key.end());
		
		if (index_pos >= FILE_NAME_LENGTH) {
			name_key_pos = name_key_pos > FILE_NAME_LENGTH ? 0 : name_key_pos;
		}
		else {
			xor_key_pos = xor_key_pos > XOR_KEY_LENGTH ? 0 : xor_key_pos;
			output_name += FILE_NAME[index_pos] ^ xor_key[xor_key_pos++];
		}
		
		Profile_Data_Vec.emplace_back(File_Vec[index_pos++] ^ output_name[name_key_pos++]);
		
	}

	constexpr uint8_t
		PROFILE_NAME_LENGTH_INDEX = 100,
		PROFILE_NAME_INDEX = 101;

	Profile_Data_Vec[PROFILE_NAME_LENGTH_INDEX] = static_cast<uchar>(FILE_NAME_LENGTH);
	std::copy(output_name.begin(), output_name.end(), Profile_Data_Vec.begin() + PROFILE_NAME_INDEX);
}