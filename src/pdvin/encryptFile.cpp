// This project uses libsodium (https://libsodium.org/) for cryptographic functions.
// Copyright (c) 2013-2025 Frank Denis <github@pureftpd.org>
uint64_t encryptFile(std::vector<uint8_t>&profile_vec, std::vector<uint8_t>&data_file_vec, std::string& data_filename, bool hasMastodonOption) {
	std::random_device rd;
 	std::mt19937 gen(rd());
	std::uniform_int_distribution<unsigned short> dis(1, 255); 
	
	const uint8_t XOR_KEY_LENGTH = 24;
	
	uint16_t 
		data_filename_xor_key_index = hasMastodonOption ? 0x1A6 : 0x15, 
		data_filename_index = hasMastodonOption ? 0x192 : 0x01;  	
											
	uint8_t 
		data_filename_length = profile_vec[data_filename_index - 1],
		data_filename_char_pos = 0;

	std::generate_n(profile_vec.begin() + data_filename_xor_key_index, XOR_KEY_LENGTH, [&dis, &gen]() { return static_cast<uint8_t>(dis(gen)); });

	while (data_filename_length--) {
		profile_vec[data_filename_index++] = data_filename[data_filename_char_pos++] ^ profile_vec[data_filename_xor_key_index++];
	}	

	uint32_t data_file_vec_size = static_cast<uint32_t>(data_file_vec.size());

	profile_vec.reserve(profile_vec.size() + data_file_vec_size);

	std::array<uint8_t, crypto_secretbox_KEYBYTES> key;	// 32 Bytes.
    	crypto_secretbox_keygen(key.data());

	std::array<uint8_t, crypto_secretbox_NONCEBYTES> nonce; // 24 Bytes.
   	randombytes_buf(nonce.data(), nonce.size());

	const uint16_t
		SODIUM_KEY_INDEX = hasMastodonOption ? 0x1BE: 0x2D,     
		NONCE_KEY_INDEX  = hasMastodonOption ? 0x1DE: 0x4D;      
	
	std::copy(key.begin(), key.end(), profile_vec.begin() + SODIUM_KEY_INDEX); 	
	std::copy(nonce.begin(), nonce.end(), profile_vec.begin() + NONCE_KEY_INDEX);

	std::vector<uint8_t> encrypted_vec(data_file_vec_size + crypto_secretbox_MACBYTES);

    	crypto_secretbox_easy(encrypted_vec.data(), data_file_vec.data(), data_file_vec_size, nonce.data(), key.data());

	profile_vec.insert(profile_vec.end(), encrypted_vec.begin(), encrypted_vec.end());
	
	std::vector<uint8_t>().swap(encrypted_vec);

	const uint64_t PIN = getByteValue<uint64_t>(profile_vec, SODIUM_KEY_INDEX); 

	uint16_t 
		sodium_xor_key_pos = SODIUM_KEY_INDEX,
		sodium_key_pos = SODIUM_KEY_INDEX;

	uint8_t
		sodium_keys_length = 48,
		value_bit_length = 64;

	sodium_key_pos += 8; 

	constexpr uint8_t SODIUM_XOR_KEY_LENGTH = 8;

	while (sodium_keys_length--) {   
    		profile_vec[sodium_key_pos] = profile_vec[sodium_key_pos] ^ profile_vec[sodium_xor_key_pos++];
		sodium_key_pos++;
    		sodium_xor_key_pos = (sodium_xor_key_pos >= SODIUM_XOR_KEY_LENGTH + SODIUM_KEY_INDEX) 
                         ? SODIUM_KEY_INDEX 
                         : sodium_xor_key_pos;
	}

	sodium_key_pos = SODIUM_KEY_INDEX;  

	std::mt19937_64 gen64(rd()); 
    	std::uniform_int_distribution<uint64_t> dis64; 

    	uint64_t random_val = dis64(gen64); 

	valueUpdater(profile_vec, sodium_key_pos, random_val, value_bit_length);

	return PIN;
}