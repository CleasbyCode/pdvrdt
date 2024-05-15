void inflateFile(std::vector<uchar>& Vec) {

	// zlib function, see https://zlib.net/

	std::vector <Byte>Buffer_Vec;

	constexpr size_t BUFSIZE = 524288;

	Byte* temp_buffer{ new Byte[BUFSIZE] };

	z_stream strm;
	strm.zalloc = 0;
	strm.zfree = 0;
	strm.next_in = Vec.data();
	strm.avail_in = static_cast<uint32_t>(Vec.size());
	strm.next_out = temp_buffer;
	strm.avail_out = BUFSIZE;

	inflateInit(&strm);

	while (strm.avail_in)
	{
		inflate(&strm, Z_NO_FLUSH);

		if (!strm.avail_out) {
			Buffer_Vec.insert(Buffer_Vec.end(), temp_buffer, temp_buffer + BUFSIZE);
			strm.next_out = temp_buffer;
			strm.avail_out = BUFSIZE;
		}
		else
			break;
	}
	inflate(&strm, Z_FINISH);
	Buffer_Vec.insert(Buffer_Vec.end(), temp_buffer, temp_buffer + BUFSIZE - strm.avail_out);
	inflateEnd(&strm);

	Vec.swap(Buffer_Vec);
	delete[] temp_buffer;
}