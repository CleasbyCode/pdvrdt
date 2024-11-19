const uint32_t encryptFile(std::vector<uint8_t>&Profile_Data_Vec, std::vector<uint8_t>&File_Vec, uint32_t data_file_size, std::string& data_filename, bool isMastodonOption) {
	
	std::random_device rd;
 	std::mt19937 gen(rd());
	std::uniform_int_distribution<unsigned short> dis(1, 255); 
	
	constexpr uint8_t 
		XOR_KEY_LENGTH = 234,
		PIN_LENGTH = 4;
	
	uint8_t 
		xor_key[XOR_KEY_LENGTH],
		xor_key_length = XOR_KEY_LENGTH,
		xor_key_pos = 0,
		char_pos = 0,
		bits = 32;

	const uint16_t DATA_FILE_START_INDEX = isMastodonOption ? 0x298 : 0x107;

	uint16_t 
		xor_key_index = isMastodonOption ? 0x1A6 : 0x15,
		data_filename_index = isMastodonOption ? 0x192 : 0x01,
		data_filename_length = Profile_Data_Vec[data_filename_index - 1],
		cover_index = xor_key_index,
		pin_index = DATA_FILE_START_INDEX,
		encrypt_xor_pos = cover_index,
		index_xor_pos = encrypt_xor_pos;

	for (int i = 0; i < XOR_KEY_LENGTH; ++i) {
        	xor_key[i] = static_cast<uint8_t>(dis(gen));
		Profile_Data_Vec[xor_key_index++] = xor_key[i];
    	}
		
	while (data_filename_length--) {
		Profile_Data_Vec[data_filename_index++] = data_filename[char_pos++] ^ xor_key[xor_key_pos++];
	}	
	
	uint32_t index_pos = 0;

	Profile_Data_Vec.reserve(Profile_Data_Vec.size() + data_file_size);

	while (data_file_size--) {
		Profile_Data_Vec.emplace_back(File_Vec[index_pos++] ^ xor_key[xor_key_pos++ % XOR_KEY_LENGTH]);
	}
		
	while(xor_key_length--) {
		Profile_Data_Vec[encrypt_xor_pos++] = Profile_Data_Vec[index_xor_pos++] ^ Profile_Data_Vec[pin_index++];
		pin_index = pin_index >= PIN_LENGTH + DATA_FILE_START_INDEX ? DATA_FILE_START_INDEX : pin_index;
	}

	pin_index = DATA_FILE_START_INDEX;

	const uint32_t 
		PIN = getByteValue(Profile_Data_Vec, pin_index),
		COVER_PIN = getByteValue(Profile_Data_Vec, cover_index);
	
	valueUpdater(Profile_Data_Vec, pin_index, COVER_PIN, bits);
	
	return PIN;
}
