
void decryptFile(std::vector<uint_fast8_t>&Image_Vec, std::vector<uint_fast8_t>&File_Vec, std::string& data_file_name) {

	std::string 
		xor_key = "\xFC\xD8\xF9\xE2\x9F\x7E",
		decrypted_file_name;

	const uint_fast8_t
		XOR_KEY_LENGTH = static_cast<uint_fast8_t>(xor_key.length()),
		data_file_name_length = static_cast<uint_fast8_t>(data_file_name.length());

	uint_fast32_t
		file_size = static_cast<uint_fast32_t>(Image_Vec.size()),
		index_pos = 0;

	uint_fast8_t
		xor_key_pos = 0,
		name_key_pos = 0;
	
	while (file_size > index_pos) {
		
		std::reverse(xor_key.begin(), xor_key.end());
		
		if (index_pos >= data_file_name_length) {
			name_key_pos = name_key_pos > data_file_name_length ? 0 : name_key_pos;
		}
		else {
			xor_key_pos = xor_key_pos > XOR_KEY_LENGTH ? 0 : xor_key_pos;
			decrypted_file_name += data_file_name[index_pos] ^ xor_key[xor_key_pos++];
		}

		File_Vec.emplace_back(Image_Vec[index_pos++] ^ data_file_name[name_key_pos++]);
	}

	data_file_name = decrypted_file_name;
} 