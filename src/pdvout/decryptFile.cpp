// This project uses libsodium (https://libsodium.org/) for cryptographic functions.
// Copyright (c) 2013-2025 Frank Denis <github@pureftpd.org>
const std::string decryptFile(std::vector<uint8_t>& image_vec, bool isMastodonFile) {
	const uint16_t 
		SODIUM_KEY_INDEX = isMastodonFile ? 0x1BE: 0x2D,
		NONCE_KEY_INDEX  = isMastodonFile ? 0x1DE: 0x4D;

	uint16_t 
		sodium_key_pos = SODIUM_KEY_INDEX,
		sodium_xor_key_pos = SODIUM_KEY_INDEX;
	
	uint8_t
		sodium_keys_length = 48,
		value_bit_length = 64;
		
	std::cout << "\nPIN: ";
	uint64_t pin = getPin();

	valueUpdater(image_vec, sodium_key_pos, pin, value_bit_length); 
	
	sodium_key_pos += 8;

	constexpr uint8_t SODIUM_XOR_KEY_LENGTH	= 8; 

	while(sodium_keys_length--) {
		image_vec[sodium_key_pos] = image_vec[sodium_key_pos] ^ image_vec[sodium_xor_key_pos++];
		sodium_key_pos++;
		sodium_xor_key_pos = (sodium_xor_key_pos >= SODIUM_XOR_KEY_LENGTH + SODIUM_KEY_INDEX) 
			? SODIUM_KEY_INDEX 
			: sodium_xor_key_pos;
	}

	std::array<uint8_t, crypto_secretbox_KEYBYTES> key;
	std::array<uint8_t, crypto_secretbox_NONCEBYTES> nonce;

	std::copy(image_vec.begin() + SODIUM_KEY_INDEX, image_vec.begin() + SODIUM_KEY_INDEX + crypto_secretbox_KEYBYTES, key.data());
	std::copy(image_vec.begin() + NONCE_KEY_INDEX, image_vec.begin() + NONCE_KEY_INDEX + crypto_secretbox_NONCEBYTES, nonce.data());

	std::string decrypted_filename;

	const uint16_t ENCRYPTED_FILENAME_INDEX = isMastodonFile ? 0x192: 0x01;

	uint16_t filename_xor_key_pos = isMastodonFile ? 0x1A6 : 0x15;
	
	uint8_t
		encrypted_filename_length = image_vec[ENCRYPTED_FILENAME_INDEX - 1],
		filename_char_pos = 0;

	const std::string ENCRYPTED_FILENAME { image_vec.begin() + ENCRYPTED_FILENAME_INDEX, image_vec.begin() + ENCRYPTED_FILENAME_INDEX + encrypted_filename_length };

	while (encrypted_filename_length--) {
		decrypted_filename += ENCRYPTED_FILENAME[filename_char_pos++] ^ image_vec[filename_xor_key_pos++];
	}

	const uint16_t ENCRYPTED_FILE_START_INDEX = isMastodonFile ? 0x1FE: 0x6E;

	image_vec.erase(image_vec.begin(), image_vec.begin() + ENCRYPTED_FILE_START_INDEX);

	std::vector<uint8_t>decrypted_file_vec(image_vec.size() - crypto_secretbox_MACBYTES);
	
	if (crypto_secretbox_open_easy(decrypted_file_vec.data(), image_vec.data(), image_vec.size(), nonce.data(), key.data()) !=0 ) {
		std::cerr << "\nDecryption failed!" << std::endl;
	}

	std::vector<uint8_t>().swap(image_vec);
	image_vec.swap(decrypted_file_vec);
	return decrypted_filename;
} 
