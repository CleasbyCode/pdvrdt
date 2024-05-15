using uchar = unsigned char;

uint32_t getFourByteValue(const std::vector<uchar>& vec, uint32_t index) {
	return	(static_cast<uint32_t>(vec[index]) << 24) |
		(static_cast<uint32_t>(vec[index + 1]) << 16) |
		(static_cast<uint32_t>(vec[index + 2]) << 8) |
		static_cast<uint32_t>(vec[index + 3]); 
}