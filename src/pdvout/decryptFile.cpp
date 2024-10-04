std::string decryptFile(std::vector<uint8_t>&Image_Vec, uint8_t (&xor_key)[XOR_KEY_LENGTH], uint32_t file_size, uint8_t filename_length, std::string& filename) {
	uint8_t
		xor_key_pos = 0,
		name_pos = 0;

	std::string decrypted_filename;

	while (filename_length--) {
		decrypted_filename += filename[name_pos++] ^ xor_key[xor_key_pos++];
	}

	uint32_t
		decrypt_pos = 0,
		index_pos = 0;

	while (file_size--) {
		Image_Vec[decrypt_pos++] = Image_Vec[index_pos++] ^ xor_key[xor_key_pos++];
		xor_key_pos = xor_key_pos >= XOR_KEY_LENGTH ? 0 : xor_key_pos;	
	}
	return decrypted_filename;
} 
