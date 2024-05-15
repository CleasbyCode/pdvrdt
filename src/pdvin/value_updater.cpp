// Writes updated values, such as chunk lengths, CRC, etc. into the relevant vector index locations.

void valueUpdater(std::vector<uchar>& vec, uint32_t value_insert_index, const uint32_t NEW_VALUE, uint8_t value_bit_length) {
	while (value_bit_length) {
		static_cast<size_t>(vec[value_insert_index++] = (NEW_VALUE >> (value_bit_length -= 8)) & 0xff);
	}
}
