// zlib function, see https://zlib.net/
uint_fast32_t deflateFile(std::vector<uint_fast8_t>& Vec, const std::string DATA_FILE_EXTENSION) {

	constexpr uint_fast32_t BUFSIZE = 2097152;
	uint_fast8_t* buffer{ new uint_fast8_t[BUFSIZE] };

	std::vector<uint_fast8_t>Deflate_Vec;

	z_stream strm;
	strm.zalloc = 0;
	strm.zfree = 0;
	strm.avail_in = static_cast<uint_fast32_t>(Vec.size());
	strm.next_in = Vec.data();
	strm.next_out = buffer;
	strm.avail_out = BUFSIZE;

	if (DATA_FILE_EXTENSION == ".zip" || DATA_FILE_EXTENSION == ".rar") {
		deflateInit(&strm, 1); // Low compression level 1.
	} else {
		deflateInit(&strm, 6); // Standard compression level 6.
	}
	
	while (strm.avail_in) {
		deflate(&strm, Z_NO_FLUSH);		
		if (!strm.avail_out) {
			Deflate_Vec.insert(Deflate_Vec.end(), buffer, buffer + BUFSIZE);
			strm.next_out = buffer;
			strm.avail_out = BUFSIZE;
		} else {
		    break;
		}
	}

	deflate(&strm, Z_FINISH);
	Deflate_Vec.insert(Deflate_Vec.end(), buffer, buffer + BUFSIZE - strm.avail_out);
	deflateEnd(&strm);

	delete[] buffer;
	Vec.swap(Deflate_Vec);

	return static_cast<uint_fast32_t>(Vec.size());
}
