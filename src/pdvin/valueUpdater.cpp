// Writes updated values, such as chunk lengths, CRC, etc. into the relevant vector index locations.
void valueUpdater(std::vector<uint8_t>& vec, uint32_t value_index, const uint32_t NEW_VALUE, uint8_t bits) {
	while (bits) {
		vec[value_index++] = (NEW_VALUE >> (bits -= 8)) & 0xff;
	}
}
