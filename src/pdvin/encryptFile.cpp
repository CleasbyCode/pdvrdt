// This project uses libsodium (https://libsodium.org/) for cryptographic functions.
// Copyright (c) 2013-2025 Frank Denis <github@pureftpd.org>

#include "encryptFile.h"
#include "getByteValue.h"
#include "valueUpdater.h"  

#define SODIUM_STATIC
#include <sodium.h>

#include <array>
#include <algorithm>
#include <iterator>
#include <random>

uint64_t encryptFile(std::vector<uint8_t>&profile_vec, std::vector<uint8_t>&data_file_vec, std::string& data_filename, bool hasMastodonOption) {
	std::random_device rd;
 	std::mt19937 gen(rd());
	std::uniform_int_distribution<unsigned short> dis(1, 255); 
	
	constexpr uint8_t XOR_KEY_LENGTH = 24;
	
	uint16_t 
		data_filename_xor_key_index = hasMastodonOption ? 0x1A6 : 0x15, 
		data_filename_index = hasMastodonOption ? 0x192 : 0x01;  	
											
	uint8_t 
		data_filename_length = profile_vec[data_filename_index - 1],
		data_filename_char_pos = 0;

	std::generate_n(profile_vec.begin() + data_filename_xor_key_index, XOR_KEY_LENGTH, [&dis, &gen]() { return static_cast<uint8_t>(dis(gen)); });

	std::transform(
        	data_filename.begin() + data_filename_char_pos, data_filename.begin() + data_filename_char_pos + data_filename_length,
        	profile_vec.begin() + data_filename_xor_key_index, profile_vec.begin() + data_filename_index,
        	[](char a, uint8_t b) { return static_cast<uint8_t>(a) ^ b; }
    	);	

	const uint32_t DATA_FILE_VEC_SIZE = static_cast<uint32_t>(data_file_vec.size());

	profile_vec.reserve(profile_vec.size() + DATA_FILE_VEC_SIZE);

	std::array<uint8_t, crypto_secretbox_KEYBYTES> key;	
    	crypto_secretbox_keygen(key.data());

	std::array<uint8_t, crypto_secretbox_NONCEBYTES> nonce; 
   	randombytes_buf(nonce.data(), nonce.size());

	const uint16_t
		SODIUM_KEY_INDEX = hasMastodonOption ? 0x1BE: 0x2D,     
		NONCE_KEY_INDEX  = hasMastodonOption ? 0x1DE: 0x4D;      
	
	std::copy_n(key.begin(), crypto_secretbox_KEYBYTES, profile_vec.begin() + SODIUM_KEY_INDEX); 	
	std::copy_n(nonce.begin(), crypto_secretbox_NONCEBYTES, profile_vec.begin() + NONCE_KEY_INDEX);

	std::vector<uint8_t> encrypted_vec(DATA_FILE_VEC_SIZE + crypto_secretbox_MACBYTES);

    	crypto_secretbox_easy(encrypted_vec.data(), data_file_vec.data(), DATA_FILE_VEC_SIZE, nonce.data(), key.data());

	std::copy_n(encrypted_vec.begin(), encrypted_vec.size(), std::back_inserter(profile_vec));

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

    	const uint64_t RANDOM_VAL = dis64(gen64); 

	valueUpdater(profile_vec, sodium_key_pos, RANDOM_VAL, value_bit_length);

	return PIN;
}
