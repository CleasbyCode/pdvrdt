const uint32_t encryptFile(std::vector<uint8_t>&Profile_Data_Vec, std::vector<uint8_t>&File_Vec, uint32_t data_file_size, std::string& data_filename) {
	std::random_device rd;
 	std::mt19937 gen(rd());
	std::uniform_int_distribution<unsigned short> dis(1, 255); 
	
	constexpr uint8_t XOR_KEY_LENGTH = 24;
		
	uint8_t xor_key[XOR_KEY_LENGTH];

	uint16_t xor_key_profile_insert_index = 0x197;
		
	for (uint8_t i = 0; XOR_KEY_LENGTH > i; ++i) {
        	xor_key[i] = static_cast<uint8_t>(dis(gen));
		Profile_Data_Vec[xor_key_profile_insert_index++] = xor_key[i];
    	}

	uint8_t
		profile_name_index = 0x65,
		data_filename_length = Profile_Data_Vec[profile_name_index - 1],
		xor_key_pos = 0,
		name_pos = 0;

	while (data_filename_length--) {
		data_filename[name_pos] = data_filename[name_pos] ^ xor_key[xor_key_pos++];
		Profile_Data_Vec[profile_name_index++] = data_filename[name_pos++];
	}

	uint32_t index_pos = 0;
	
	Profile_Data_Vec.reserve(Profile_Data_Vec.size() + data_file_size);

	while (data_file_size--) {
		Profile_Data_Vec.emplace_back(File_Vec[index_pos++] ^ xor_key[xor_key_pos++ % XOR_KEY_LENGTH]);
	}
	return static_cast<uint32_t>(Profile_Data_Vec.size());
}	
