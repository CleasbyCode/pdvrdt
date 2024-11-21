const std::string decryptFile(std::vector<uint8_t>&Image_Vec, std::vector<uint8_t>&Decrypted_File_Vec, bool isMastodonFile) {
	
	constexpr uint8_t 
		DEFAULT_PIN_INDEX = 0x04, 	// CRC index location. Used as our recovery PIN.
		DEFAULT_PIN_XOR_INDEX = 0x00, 	// Start index location of the 8 byte xor key, used to decrypt the main xor key.
		XOR_KEY_LENGTH = 234,
		PIN_XOR_LENGTH = 8;

	const uint16_t 
		ENCRYPTED_FILENAME_INDEX = isMastodonFile ? 0x192 : 0x01,
		DATA_FILE_INDEX = isMastodonFile ? 0x298 : 0x107;	
	
	uint8_t 
		Xor_Key_Arr[XOR_KEY_LENGTH],
		xor_key_length = XOR_KEY_LENGTH,
		xor_key_pos = 0,
		name_pos = 0,
		value_bit_length = 32;

	uint16_t 
		encrypted_filename_length = Image_Vec[ENCRYPTED_FILENAME_INDEX - 1],
		xor_key_index = isMastodonFile ? 0x1A6 : 0x15,
		pin_index = DEFAULT_PIN_INDEX,
		pin_xor_index = DEFAULT_PIN_XOR_INDEX,

		decrypt_xor_pos = xor_key_index,
		index_xor_pos = decrypt_xor_pos;

	std::cout << "\nPIN: ";
	uint32_t pin = getPin();

	std::vector<uint8_t> Pin_Vec; 	// Temporary vector used to store the 4 byte CRC/PIN value and an additional 4 random bytes.
	Pin_Vec.resize(8);		// This will be our 8 byte xor key, used to decrypt the main xor key.

	const uint32_t PIN_XOR_KEY = getByteValue(Image_Vec, ENCRYPTED_FILENAME_INDEX - 1);  // Get the random 4 bytes.
	
	valueUpdater(Pin_Vec, pin_xor_index, PIN_XOR_KEY, value_bit_length); 	// Write the random 4 bytes to vector index location	
	valueUpdater(Pin_Vec, pin_index, pin, value_bit_length);		// Write the 4 byte PIN/CRC to vector index location. This completes the 8 byte xor key.

	// Decrypt main xor key using above 8 byte xor key.
	while(xor_key_length--) {
		Image_Vec[decrypt_xor_pos++] = Image_Vec[index_xor_pos++] ^ Image_Vec[pin_xor_index++ % PIN_XOR_LENGTH];
	}

	// Check to make sure correct PIN value was used to decrypt main xor key.
	// Supplied PIN value should match CRC value from this data selection.
	const uint32_t CRC_CHECK = crcUpdate(&Image_Vec[xor_key_index], XOR_KEY_LENGTH);

	if (pin != CRC_CHECK) {  // Failure. Incorrect PIN used, main xor key not correctly decrypted, so CRC check value will not match PIN.
		std::reverse(Image_Vec.begin(), Image_Vec.end()); // Destory the data. Recovery not possible. Try again...
	}

	// Fill xor key array with correct/decrypted main 234 byte xor key.
	for (int i = 0; XOR_KEY_LENGTH > i; ++i) {
		Xor_Key_Arr[i] = Image_Vec[xor_key_index++]; 
	}

	const std::string ENCRYPTED_FILENAME = { Image_Vec.begin() + ENCRYPTED_FILENAME_INDEX, Image_Vec.begin() + ENCRYPTED_FILENAME_INDEX + encrypted_filename_length };
	
	std::string decrypted_filename;

	// Decrypt filename of hidden file.
	while (encrypted_filename_length--) {
		decrypted_filename += ENCRYPTED_FILENAME[name_pos++] ^ Xor_Key_Arr[xor_key_pos++];
	}
	
	// Wipe the header/profile data. Just the encrypted/compressed data file remains.
	Image_Vec.erase(Image_Vec.begin(), Image_Vec.begin() + DATA_FILE_INDEX);

	uint32_t
		encrypted_file_size = static_cast<uint32_t>(Image_Vec.size()),
		index_pos = 0;

	// Decrypt hidden data file.
	while (encrypted_file_size > index_pos) {
		Decrypted_File_Vec.emplace_back(Image_Vec[index_pos++] ^ Xor_Key_Arr[xor_key_pos++ % XOR_KEY_LENGTH]);
	}

	std::vector<uint8_t>().swap(Image_Vec);
	return decrypted_filename;
} 
