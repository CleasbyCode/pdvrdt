// zlib function, see https://zlib.net/
void deflateFile(std::vector<uint8_t>& vec, bool hasMastodonOption, bool isCompressedFile) {
	constexpr uint32_t 
		BUFSIZE = 2 * 1024 * 1024, // 2MB	
		LARGE_FILE_SIZE	 = 500 * 1024 * 1024,  //  > 500MB.
		MEDIUM_FILE_SIZE = 200 * 1024 * 1024,  //  > 200MB.
		COMPRESSED_FILE_TYPE_SIZE_LIMIT = 50 * 1024 * 1024; // > 50MB. Skip compressing already compressed files over 50MB.

	const uint32_t VEC_SIZE = static_cast<uint32_t>(vec.size());

	uint8_t* buffer{ new uint8_t[BUFSIZE] };
	
	std::vector<uint8_t>deflate_vec;
	deflate_vec.reserve(VEC_SIZE + BUFSIZE);
	
	z_stream strm;
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.next_in = vec.data();
	strm.avail_in = VEC_SIZE;
	strm.next_out = buffer;
	strm.avail_out = BUFSIZE;

	int8_t compression_level;

	if (isCompressedFile && VEC_SIZE > COMPRESSED_FILE_TYPE_SIZE_LIMIT) {
	    compression_level = Z_NO_COMPRESSION;
	} else if (VEC_SIZE > LARGE_FILE_SIZE) {
	    compression_level = Z_BEST_SPEED;
	} else if (VEC_SIZE > MEDIUM_FILE_SIZE || hasMastodonOption) {
	    compression_level = Z_DEFAULT_COMPRESSION;
	} else {
	    compression_level = Z_BEST_COMPRESSION;
	}

	deflateInit(&strm, compression_level);

	while (strm.avail_in)
	{
		deflate(&strm, Z_NO_FLUSH);
		
		if (!strm.avail_out) {
			deflate_vec.insert(deflate_vec.end(), buffer, buffer + BUFSIZE);
			strm.next_out = buffer;
			strm.avail_out = BUFSIZE;
		} else {
			break;
		}
	}
	
	deflate(&strm, Z_FINISH);
	deflate_vec.insert(deflate_vec.end(), buffer, buffer + BUFSIZE - strm.avail_out);
	deflateEnd(&strm);

	delete[] buffer;	
	vec = std::move(deflate_vec);
	std::vector<uint8_t>().swap(deflate_vec);
}