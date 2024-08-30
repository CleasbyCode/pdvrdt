std::string decryptFile(std::vector<uint_fast8_t>&Image_Vec, uint_fast8_t (&xor_key)[XOR_KEY_LENGTH], std::string& encrypted_data_filename) {

	const uint_fast32_t ENCRYPTED_DATA_FILE_SIZE = static_cast<uint_fast32_t>(Image_Vec.size());

	const uint_fast8_t ENCRYPTED_DATA_FILENAME_LENGTH = static_cast<uint_fast8_t>(encrypted_data_filename.length());

	uint_fast32_t
		decrypt_pos{},
		index_pos{};

	uint_fast8_t
		xor_key_pos{},
		name_key_pos{};

	std::string decrypted_data_filename;

	while (ENCRYPTED_DATA_FILE_SIZE > index_pos) {
		
		if (index_pos >= ENCRYPTED_DATA_FILENAME_LENGTH) {
			name_key_pos = name_key_pos >= ENCRYPTED_DATA_FILENAME_LENGTH ? 0 : name_key_pos;
		}
		else {
			std::reverse(std::begin(xor_key), std::end(xor_key));
			xor_key_pos = xor_key_pos >= XOR_KEY_LENGTH ? 0 : xor_key_pos;
			decrypted_data_filename += encrypted_data_filename[index_pos] ^ xor_key[xor_key_pos++];
		}
		Image_Vec[decrypt_pos++] = Image_Vec[index_pos++] ^ encrypted_data_filename[name_key_pos++];
	}
	return decrypted_data_filename;
} 
