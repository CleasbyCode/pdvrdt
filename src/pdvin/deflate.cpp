// zlib function, see https://zlib.net/

void deflateFile(std::vector<uchar>& Vec) {
	
	std::vector<uchar>Buffer_Vec;

	constexpr uint32_t BUFSIZE = 524288;

	uchar* temp_buffer{ new uchar[BUFSIZE] };

	z_stream strm;
	strm.zalloc = 0;
	strm.zfree = 0;
	strm.next_in = Vec.data();
	strm.avail_in = static_cast<uint32_t>(Vec.size());
	strm.next_out = temp_buffer;
	strm.avail_out = BUFSIZE;

	deflateInit(&strm, 9); // Compression level 9
	
	while (strm.avail_in)
	{
		deflate(&strm, Z_NO_FLUSH);
		
		if (!strm.avail_out) {
			Buffer_Vec.insert(Buffer_Vec.end(), temp_buffer, temp_buffer + BUFSIZE);
			strm.next_out = temp_buffer;
			strm.avail_out = BUFSIZE;
		}
		else
			break;
	}
	deflate(&strm, Z_FINISH);
	Buffer_Vec.insert(Buffer_Vec.end(), temp_buffer, temp_buffer + BUFSIZE - strm.avail_out);
	deflateEnd(&strm);
	
	Vec.swap(Buffer_Vec);
	delete[] temp_buffer;
}
