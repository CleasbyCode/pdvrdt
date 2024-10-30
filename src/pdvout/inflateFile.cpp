// zlib function, see https://zlib.net/
void inflateFile(std::vector<uint8_t>& Vec) {
	constexpr uint32_t BUFSIZE = 2 * 1024 * 1024; // 2MB
	
	const uint32_t VEC_SIZE = static_cast<uint32_t>(Vec.size());
	
	uint8_t* buffer{ new uint8_t[BUFSIZE] };
	
	std::vector<uint8_t>Inflate_Vec;
	Inflate_Vec.reserve(VEC_SIZE + BUFSIZE);

	z_stream strm;
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.next_in = Vec.data();
	strm.avail_in = VEC_SIZE;
	strm.next_out = buffer;
	strm.avail_out = BUFSIZE;

	inflateInit(&strm);

	while (strm.avail_in) {
		inflate(&strm, Z_NO_FLUSH);

		if (!strm.avail_out) {
			Inflate_Vec.insert(Inflate_Vec.end(), buffer, buffer + BUFSIZE);
			strm.next_out = buffer;
			strm.avail_out = BUFSIZE;
		} else {
		    break;
		}
	}

	inflate(&strm, Z_FINISH);
	Inflate_Vec.insert(Inflate_Vec.end(), buffer, buffer + BUFSIZE - strm.avail_out);
	inflateEnd(&strm);
	
	delete[] buffer;
	Vec = std::move(Inflate_Vec);
	std::vector<uint8_t>().swap(Inflate_Vec);
}
