void copyEssentialChunks(std::vector<uint8_t>& image_vec, const size_t IMAGE_VEC_SIZE) {
	constexpr std::array<uint8_t, 4> 
		PLTE_SIG {0x50, 0x4C, 0x54, 0x45},
		IDAT_SIG {0x49, 0x44, 0x41, 0x54};

    	constexpr uint8_t
		PNG_FIRST_BYTES = 33,
		PNG_IEND_BYTES = 12,
		PNG_COLOR_TYPE_INDEX = 0x19,
		PNG_INDEXED_COLOR_VAL = 3;
		
    	std::vector<uint8_t> copied_image_vec;
    	copied_image_vec.reserve(IMAGE_VEC_SIZE);     
  
    	copied_image_vec.insert(copied_image_vec.begin(), image_vec.begin(), image_vec.begin() + PNG_FIRST_BYTES);	

    	auto copy_chunk_type = [&](const auto& chunk_signature) {
		constexpr uint8_t 
			PNG_CHUNK_FIELDS_OVERHEAD = 12,
			PNG_CHUNK_LENGTH_FIELD_SIZE = 4,
			SEARCH_INCREMENT = 5;

        	uint32_t 
			chunk_search_pos = 0,
			chunk_length_pos = 0,
			chunk_length = 0;

        	while (true) {
            		chunk_search_pos = searchFunc(image_vec, chunk_search_pos, SEARCH_INCREMENT, chunk_signature);
			if (chunk_search_pos == IMAGE_VEC_SIZE) {
				break;
			}
			chunk_length_pos = chunk_search_pos - PNG_CHUNK_LENGTH_FIELD_SIZE;
            		chunk_length = getByteValue<uint32_t>(image_vec, chunk_length_pos) + PNG_CHUNK_FIELDS_OVERHEAD;
	    		std::copy_n(image_vec.begin() + chunk_length_pos, chunk_length, std::back_inserter(copied_image_vec));
        	}
    	};
	if (image_vec[PNG_COLOR_TYPE_INDEX] == PNG_INDEXED_COLOR_VAL) {
    		copy_chunk_type(PLTE_SIG);
	}
    	copy_chunk_type(IDAT_SIG);
    	copied_image_vec.insert(copied_image_vec.end(), image_vec.end() - PNG_IEND_BYTES, image_vec.end());
    	image_vec = std::move(copied_image_vec);
}