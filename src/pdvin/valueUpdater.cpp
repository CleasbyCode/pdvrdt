// Writes updated values, such as chunk lengths, CRC, etc. into the relevant vector index locations.
void valueUpdater(std::vector<uint8_t>& vec, uint_fast32_t value_insert_index, const uint_fast32_t NEW_VALUE, uint_fast8_t bits) {
	while (bits) {
		vec[value_insert_index++] = (NEW_VALUE >> (bits -= 8)) & 0xff;
	}
}
