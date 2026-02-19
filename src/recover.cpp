#include "recover.h"
#include "encryption.h"
#include "png_utils.h"
#include "compression.h"

namespace {

struct ChunkLocation {
	std::size_t
		data_start,
		data_size;
	bool is_mastodon;
};

ChunkLocation locateEmbeddedData(const vBytes& png_vec) {
	constexpr auto
		ICCP_SIG = std::to_array<Byte>({ 0x69, 0x43, 0x43, 0x50, 0x69, 0x63, 0x63 }),
		PDV_SIG  = std::to_array<Byte>({ 0xC6, 0x50, 0x3C, 0xEA, 0x5E, 0x9D, 0xF9 });

	constexpr std::size_t
		EXPECTED_ICCP_INDEX = 0x25,
		// Mastodon: iCCP chunk layout offsets.
		MASTODON_SIZE_INDEX_OFFSET = 4,   // chunk size field is 4 bytes before iCCP signature
		MASTODON_DATA_OFFSET       = 9,   // profile data starts 9 bytes after iCCP signature
		MASTODON_HEADER_OVERHEAD   = 9,   // iCCP chunk header bytes not part of profile data
		MASTODON_TAIL_PAD          = 3,
		// Default: custom IDAT chunk layout offsets.
		DEFAULT_SIZE_INDEX_OFFSET  = 112, // chunk size field is 112 bytes before PDV signature
		DEFAULT_DATA_OFFSET        = 11,  // profile data starts 11 bytes after chunk size field
		DEFAULT_TAIL_PAD           = 3;

	const auto
		iccp_opt = searchSig(png_vec, ICCP_SIG),
		pdv_opt  = searchSig(png_vec, PDV_SIG);

	const bool
		has_iccp_at_expected = iccp_opt && (*iccp_opt == EXPECTED_ICCP_INDEX),
		has_pdv = pdv_opt.has_value();

	if (!has_iccp_at_expected && !has_pdv) {
		throw std::runtime_error("Image File Error: This is not a pdvrdt image.");
	}

	// Mastodon files have iCCP at the expected index and no visible PDV signature
	// (PDV signature is inside the deflated iCCP data).
	if (has_iccp_at_expected && !has_pdv) {
		const std::size_t
			iccp_index = *iccp_opt,
			chunk_size_index = iccp_index - MASTODON_SIZE_INDEX_OFFSET,
			raw_chunk_size = getValue(png_vec, chunk_size_index);

		return {
			.data_start  = iccp_index + MASTODON_DATA_OFFSET,
			.data_size   = raw_chunk_size - MASTODON_HEADER_OVERHEAD + MASTODON_TAIL_PAD,
			.is_mastodon = true
		};
	}

	// Default mode: PDV signature found directly.
	const std::size_t
		pdv_index = *pdv_opt,
		chunk_size_index = pdv_index - DEFAULT_SIZE_INDEX_OFFSET,
		chunk_size = getValue(png_vec, chunk_size_index);

	return {
		.data_start  = chunk_size_index + DEFAULT_DATA_OFFSET,
		.data_size   = chunk_size - DEFAULT_TAIL_PAD,
		.is_mastodon = false
	};
}

} // namespace

void recoverData(vBytes& png_vec) {
	constexpr auto PDV_SIG = std::to_array<Byte>({ 0xC6, 0x50, 0x3C, 0xEA, 0x5E, 0x9D, 0xF9 });

	const auto location = locateEmbeddedData(png_vec);

	// Extract just the embedded data, discarding the rest of the PNG.
	png_vec.erase(png_vec.begin() + location.data_start + location.data_size, png_vec.end());
	png_vec.erase(png_vec.begin(), png_vec.begin() + location.data_start);

	if (location.is_mastodon) {
		zlibInflate(png_vec);

		if (png_vec.empty()) {
			throw std::runtime_error("File Size Error: File is zero bytes. Probable failure inflating file.");
		}

		if (!searchSig(png_vec, PDV_SIG)) {
			throw std::runtime_error("Image File Error: This is not a pdvrdt image.");
		}
	}

	auto result = decryptDataFile(png_vec, location.is_mastodon);
	if (!result) {
		throw std::runtime_error("File Recovery Error: Invalid PIN or file is corrupt.");
	}

	std::string decrypted_filename = std::move(*result);

	zlibInflate(png_vec);

	if (png_vec.empty()) {
		throw std::runtime_error("Zlib Compression Error: Output file is empty. Inflating file failed.");
	}

	std::ofstream file_ofs(decrypted_filename, std::ios::binary);
	if (!file_ofs) {
		throw std::runtime_error("Write File Error: Unable to write to file.");
	}

	file_ofs.write(reinterpret_cast<const char*>(png_vec.data()), png_vec.size());

	std::println("\nExtracted hidden file: {} ({} bytes).\n\nComplete! Please check your file.\n",
		decrypted_filename, png_vec.size());
}
