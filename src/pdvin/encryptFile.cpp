const uint32_t encryptFile(std::vector<uint8_t>&Profile_Data_Vec, std::vector<uint8_t>&File_Vec, uint32_t data_file_size, std::string& data_filename, bool isMastodonOption) {
	
	std::random_device rd;
 	std::mt19937 gen(rd());
	std::uniform_int_distribution<unsigned short> dis(1, 255); // Used to generate our random xor key.
	
	constexpr uint8_t 
		DEFAULT_PIN_INDEX = 0x04, 	// CRC index location. Used as our recovery PIN.
		DEFAULT_PIN_XOR_INDEX = 0x00, 	// Start index location of the 8 byte xor key, used to encrypt the main xor key.
		XOR_KEY_LENGTH = 234, 	// Main xor key length.
		PIN_XOR_LENGTH = 8;	// Second xor key length.	
	
	uint8_t 
		xor_key[XOR_KEY_LENGTH],
		xor_key_length = XOR_KEY_LENGTH,
		xor_key_pos = 0,
		char_pos = 0,
		value_bit_length = 32;

	const uint16_t 
		DATA_FILE_START_INDEX = isMastodonOption ? 0x298 : 0x107;

	uint16_t 
		xor_key_index = isMastodonOption ? 0x1A6 : 0x15,
		data_filename_index = isMastodonOption ? 0x192 : 0x01,
		data_filename_length = Profile_Data_Vec[data_filename_index - 1],
		pin_index = DEFAULT_PIN_INDEX,
		pin_xor_index = DEFAULT_PIN_XOR_INDEX,
		encrypt_xor_pos = xor_key_index,
		index_xor_pos = encrypt_xor_pos;

	// Get 234 random bytes and fill the xor_key array.
	for (int i = 0; i < XOR_KEY_LENGTH; ++i) {
        	xor_key[i] = static_cast<uint8_t>(dis(gen));
		Profile_Data_Vec[xor_key_index++] = xor_key[i];
    	}
	
	// Encrypt filename of hidden file.	
	while (data_filename_length--) {
		data_filename[char_pos] = data_filename[char_pos] ^ xor_key[xor_key_pos++];
		Profile_Data_Vec[data_filename_index++] = data_filename[char_pos++];
	}	
	
	uint32_t index_pos = 0;

	Profile_Data_Vec.reserve(Profile_Data_Vec.size() + data_file_size);

	// Encrypt hidden data file.
	while (data_file_size--) {
		Profile_Data_Vec.emplace_back(File_Vec[index_pos++] ^ xor_key[xor_key_pos++ % XOR_KEY_LENGTH]);
	}
	
	std::vector<uint8_t> Pin_Vec; 	// Temporary vector used to store the 4 byte CRC/PIN value and an additional 4 random bytes.
	Pin_Vec.resize(8);		// This will be our 8 byte xor key, used to encrypt the main xor key.

	const uint32_t PIN_XOR_KEY = getByteValue(Profile_Data_Vec, data_filename_index - 1);  // Get the random 4 bytes.
	
	valueUpdater(Pin_Vec, pin_xor_index, PIN_XOR_KEY, value_bit_length); // Write the random 4 bytes to vector index location		

	xor_key_index = isMastodonOption ? 0x1A6 : 0x15;
	const uint32_t PIN = crcUpdate(&Profile_Data_Vec[xor_key_index], XOR_KEY_LENGTH); // Get CRC (PIN) value for the selected data area.
	
	valueUpdater(Pin_Vec, pin_index, PIN, value_bit_length); // Write CRC (PIN) value to vector index location.

	// Encrypt main xor key.
	while(xor_key_length--) {
		Profile_Data_Vec[encrypt_xor_pos++] = Profile_Data_Vec[index_xor_pos++] ^ Profile_Data_Vec[pin_xor_index++];
		pin_xor_index = pin_xor_index >= PIN_XOR_LENGTH + DEFAULT_PIN_XOR_INDEX ? DEFAULT_PIN_XOR_INDEX : pin_xor_index;
	}

	return PIN;
}