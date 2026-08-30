#pragma once

#include "common.h"

#include <cstdint>
#include <optional>
#include <span>

// Decoded, normalized carrier state for Reddit's metadata-free PNG path.
// The RGB samples are retained until encryption has completed so the final
// payload can be embedded without decoding or transcoding the cover twice.
struct RedditPngCarrier {
	vBytes rgb{};
	std::uint32_t width{};
	std::uint32_t height{};
	std::size_t payload_capacity{};
};

// Decode the already validated/optimized pdvrdt cover to opaque RGB8 and
// calculate the exact adaptive (1,15,4) matrix-embedding carrier capacity.
[[nodiscard]] RedditPngCarrier prepareRedditPngCarrier(std::span<const Byte> png);

// Embed a raw payload using the PNGSTEG1 carrier format and encode a
// non-interlaced, metadata-free RGB PNG. `carrier_key` fixes every sample
// position and whitening bit; see deriveCarrierKeyFromPin().
[[nodiscard]] vBytes embedRedditPngPayload(
	RedditPngCarrier& carrier,
	std::uint64_t carrier_key,
	std::span<const Byte> payload);

// Return nullopt for an ordinary PNG, for a PNG whose carrier does not decode
// under `carrier_key` (a wrong PIN is indistinguishable from no carrier -- that
// is the point of keying it), or for a CRC-valid generic PNGSTEG1 carrier that
// is not a pdvrdt profile. Once a PNGSTEG1 header is identified under this key,
// structural or CRC failures are reported as corruption.
[[nodiscard]] std::optional<vBytes> extractRedditPngPayload(
	std::span<const Byte> png,
	std::uint64_t carrier_key);
