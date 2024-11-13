const std::string decryptFile(std::vector<uint8_t>&Image_Vec, std::vector<uint8_t>&Decrypted_File_Vec, bool isMastodonFile) {
	
	constexpr uint8_t 
		XOR_KEY_LENGTH = 234,
		PIN_LENGTH = 4;

	const uint16_t 
		ENCRYPTED_FILENAME_INDEX = isMastodonFile ? 0x192 : 0x01,
		DATA_FILE_INDEX = isMastodonFile ? 0x298 : 0x107;	
	
	uint8_t 
		Xor_Key_Arr[XOR_KEY_LENGTH],
		xor_key_length = XOR_KEY_LENGTH,
		xor_key_pos = 0,
		name_pos = 0,
		bits = 32;

	uint16_t 
		encrypted_filename_length = Image_Vec[ENCRYPTED_FILENAME_INDEX - 1],
		xor_key_index = isMastodonFile ? 0x1A6 : 0x15,
		pin_index = DATA_FILE_INDEX,
		decrypt_xor_pos = xor_key_index,
		index_xor_pos = decrypt_xor_pos;

	std::cout << "\nPIN: ";
	uint32_t pin = getPin();

	valueUpdater(Image_Vec, pin_index, pin, bits);

	while(xor_key_length--) {
		Image_Vec[decrypt_xor_pos++] = Image_Vec[index_xor_pos++] ^ Image_Vec[pin_index++];
		pin_index = pin_index >= PIN_LENGTH + DATA_FILE_INDEX ? DATA_FILE_INDEX : pin_index;
	}

	for (int i = 0; XOR_KEY_LENGTH > i; ++i) {
		Xor_Key_Arr[i] = Image_Vec[xor_key_index++]; 
	}

	const std::string ENCRYPTED_FILENAME = { Image_Vec.begin() + ENCRYPTED_FILENAME_INDEX, Image_Vec.begin() + ENCRYPTED_FILENAME_INDEX + encrypted_filename_length };
	
	std::string decrypted_filename;

	while (encrypted_filename_length--) {
		decrypted_filename += ENCRYPTED_FILENAME[name_pos++] ^ Xor_Key_Arr[xor_key_pos++];
	}
	
	// Wipe the header/profile data. Just the encrypted/compressed data file remains.
	Image_Vec.erase(Image_Vec.begin(), Image_Vec.begin() + DATA_FILE_INDEX);

	uint32_t
		encrypted_file_size = static_cast<uint32_t>(Image_Vec.size()),
		index_pos = 0;

	while (encrypted_file_size > index_pos) {
		Decrypted_File_Vec.emplace_back(Image_Vec[index_pos++] ^ Xor_Key_Arr[xor_key_pos++ % XOR_KEY_LENGTH]);
	}

	std::vector<uint8_t>().swap(Image_Vec);
	return decrypted_filename;
} 
