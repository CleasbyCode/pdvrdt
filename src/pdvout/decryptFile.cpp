std::string decryptFile(std::vector<uint8_t>&Image_Vec, const uint8_t* XOR_KEY_ARR, const std::string& ENCRYPTED_FILENAME) {
	constexpr uint8_t XOR_KEY_LENGTH = 24;

	uint32_t
		encrypted_file_size = static_cast<uint32_t>(Image_Vec.size()),
		decrypt_pos = 0,
		index_pos = 0;

	uint8_t
		encrypted_filename_length = static_cast<uint8_t>(ENCRYPTED_FILENAME.length()),
		xor_key_pos = 0,
		name_pos = 0;

	std::string decrypted_filename;

	while (encrypted_filename_length--) {
		decrypted_filename += ENCRYPTED_FILENAME[name_pos++] ^ XOR_KEY_ARR[xor_key_pos++];
	}

	while (encrypted_file_size--) {
		Image_Vec[decrypt_pos++] = Image_Vec[index_pos++] ^ XOR_KEY_ARR[xor_key_pos++];
		xor_key_pos = xor_key_pos >= XOR_KEY_LENGTH ? 0 : xor_key_pos;	
	}
	return decrypted_filename;
} 
