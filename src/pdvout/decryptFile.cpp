// This project uses libsodium (https://libsodium.org/) for cryptographic functions.
// Copyright (c) 2013-2025 Frank Denis <github@pureftpd.org>
const std::string decryptFile(std::vector<uint8_t>&Image_Vec, bool isMastodonFile) {
	
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

	valueUpdater(Image_Vec, sodium_key_pos, pin, value_bit_length); 
	
	sodium_key_pos += 8;

	constexpr uint8_t SODIUM_XOR_KEY_LENGTH	= 8; 

	while(sodium_keys_length--) {
		Image_Vec[sodium_key_pos] = Image_Vec[sodium_key_pos] ^ Image_Vec[sodium_xor_key_pos++];
		sodium_key_pos++;
		sodium_xor_key_pos = (sodium_xor_key_pos >= SODIUM_XOR_KEY_LENGTH + SODIUM_KEY_INDEX) 
			? SODIUM_KEY_INDEX 
			: sodium_xor_key_pos;
	}

	uint8_t nonce[crypto_secretbox_NONCEBYTES];
	uint8_t key[crypto_secretbox_KEYBYTES];

	std::copy(Image_Vec.begin() + NONCE_KEY_INDEX, Image_Vec.begin() + NONCE_KEY_INDEX + crypto_secretbox_NONCEBYTES, nonce);
	std::copy(Image_Vec.begin() + SODIUM_KEY_INDEX, Image_Vec.begin() + SODIUM_KEY_INDEX + crypto_secretbox_KEYBYTES, key);

	std::string decrypted_filename;

	const uint16_t ENCRYPTED_FILENAME_INDEX = isMastodonFile ? 0x192: 0x01;

	uint16_t filename_xor_key_pos = isMastodonFile ? 0x1A6 : 0x15;
	
	uint8_t
		encrypted_filename_length = Image_Vec[ENCRYPTED_FILENAME_INDEX - 1],
		filename_char_pos = 0;

	const std::string ENCRYPTED_FILENAME { Image_Vec.begin() + ENCRYPTED_FILENAME_INDEX, Image_Vec.begin() + ENCRYPTED_FILENAME_INDEX + encrypted_filename_length };

	while (encrypted_filename_length--) {
		decrypted_filename += ENCRYPTED_FILENAME[filename_char_pos++] ^ Image_Vec[filename_xor_key_pos++];
	}

	const uint16_t ENCRYPTED_FILE_START_INDEX = isMastodonFile ? 0x1FE: 0x6E;

	Image_Vec.erase(Image_Vec.begin(), Image_Vec.begin() + ENCRYPTED_FILE_START_INDEX);

	std::vector<uint8_t>Decrypted_File_Vec(Image_Vec.size() - crypto_secretbox_MACBYTES);
	
	if (crypto_secretbox_open_easy(Decrypted_File_Vec.data(), Image_Vec.data(), Image_Vec.size(), nonce, key) !=0 ) {
		std::cerr << "\nDecryption failed!" << std::endl;
	}

	std::vector<uint8_t>().swap(Image_Vec);
	Image_Vec.swap(Decrypted_File_Vec);
	return decrypted_filename;
} 