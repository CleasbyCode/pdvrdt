
void decryptFile(std::vector<uint_fast8_t>&Image_Vec, std::vector<uint_fast8_t>&File_Vec, uint_fast8_t (&xor_key)[XOR_KEY_LENGTH], std::string& encrypted_file_name) {

	std::string decrypted_file_name;

	const uint_fast8_t FILE_NAME_LENGTH = static_cast<uint_fast8_t>(encrypted_file_name.length());

	uint_fast32_t
		file_size = static_cast<uint_fast32_t>(Image_Vec.size()),
		index_pos{};

	uint_fast8_t
		xor_key_pos{},
		name_key_pos{};

	while (file_size > index_pos) {
		
		std::reverse(std::begin(xor_key), std::end(xor_key));
		
		if (index_pos >= FILE_NAME_LENGTH) {
			name_key_pos = name_key_pos >= FILE_NAME_LENGTH ? 0 : name_key_pos;
		}
		else {
			xor_key_pos = xor_key_pos >= XOR_KEY_LENGTH ? 0 : xor_key_pos;
			decrypted_file_name += encrypted_file_name[index_pos] ^ xor_key[xor_key_pos++];
		}

		File_Vec.emplace_back(Image_Vec[index_pos++] ^ encrypted_file_name[name_key_pos++]);
	}

	encrypted_file_name = decrypted_file_name;
} 