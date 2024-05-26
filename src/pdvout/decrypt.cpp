
void decryptFile(std::vector<uint_fast8_t>&Image_Vec, uint_fast8_t (&xor_key)[XOR_KEY_LENGTH], std::string& encrypted_file_name) {

	const uint_fast32_t DATA_FILE_SIZE = static_cast<uint_fast32_t>(Image_Vec.size());
	const uint_fast8_t FILE_NAME_LENGTH = static_cast<uint_fast8_t>(encrypted_file_name.length());

	uint_fast32_t
		decrypt_pos{},
		index_pos{};
	uint_fast8_t
		xor_key_pos{},
		name_key_pos{};

	std::string decrypted_file_name;

	while (DATA_FILE_SIZE > index_pos) {
		
		if (index_pos >= FILE_NAME_LENGTH) {
			name_key_pos = name_key_pos >= FILE_NAME_LENGTH ? 0 : name_key_pos;
		}
		else {
			std::reverse(std::begin(xor_key), std::end(xor_key));
			xor_key_pos = xor_key_pos >= XOR_KEY_LENGTH ? 0 : xor_key_pos;
			decrypted_file_name += encrypted_file_name[index_pos] ^ xor_key[xor_key_pos++];
		}
		Image_Vec[decrypt_pos++] = Image_Vec[index_pos++] ^ encrypted_file_name[name_key_pos++];
	}
	encrypted_file_name = decrypted_file_name;
} 