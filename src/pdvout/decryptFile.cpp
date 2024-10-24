const std::string decryptFile(std::vector<uint8_t>&Image_Vec) {
	constexpr uint8_t 
		XOR_KEY_LENGTH = 24,
		ENCRYPTED_FILENAME_INDEX = 0x65;

	constexpr uint16_t DATA_FILE_INDEX = 0x1AF;	
	
	uint8_t Xor_Key_Arr[XOR_KEY_LENGTH];

	uint16_t xor_key_index = 0x197;

	// Get the xor_key from the profile.
	for (uint8_t i = 0; XOR_KEY_LENGTH > i; ++i) {
		Xor_Key_Arr[i] = Image_Vec[xor_key_index++]; 
	}

	uint8_t
		encrypted_filename_length = Image_Vec[ENCRYPTED_FILENAME_INDEX - 1],
		xor_key_pos = 0,
		name_pos = 0;
	
	const std::string ENCRYPTED_FILENAME = { Image_Vec.begin() + ENCRYPTED_FILENAME_INDEX, Image_Vec.begin() + ENCRYPTED_FILENAME_INDEX + encrypted_filename_length };

	std::string decrypted_filename;

	while (encrypted_filename_length--) {
		decrypted_filename += ENCRYPTED_FILENAME[name_pos++] ^ Xor_Key_Arr[xor_key_pos++];
	}
	
	Image_Vec.erase(Image_Vec.begin(), Image_Vec.begin() + DATA_FILE_INDEX);

	uint32_t
		encrypted_file_size = static_cast<uint32_t>(Image_Vec.size()),
		decrypt_pos = 0,
		index_pos = 0;

	while (encrypted_file_size--) {
		Image_Vec[decrypt_pos++] = Image_Vec[index_pos++] ^ Xor_Key_Arr[xor_key_pos++ % XOR_KEY_LENGTH];
	}
	return decrypted_filename;
} 
