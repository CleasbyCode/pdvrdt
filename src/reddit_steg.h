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
// non-interlaced, metadata-free RGB PNG using carrier version 2. `carrier_key`
// must come from deriveCarrierKeyFromPin(), which uses Argon2id.
[[nodiscard]] vBytes embedRedditPngPayload(
	RedditPngCarrier& carrier,
	std::uint64_t carrier_key,
	std::span<const Byte> payload);

// Return nullopt for an ordinary PNG, for a PNG whose carrier does not decode
// under `recovery_pin`, or for a CRC-valid generic PNGSTEG1 carrier that is not a
// pdvrdt profile. Decode once, then try the version-2 Argon2id key and, only if
// no header is found, the legacy version-1 key. Each key accepts only its own
// header version. Once a header is identified, structural or CRC failures are
// reported as corruption. KDF failures propagate without a legacy fallback.
[[nodiscard]] std::optional<vBytes> extractRedditPngPayload(
	std::span<const Byte> png,
	const SensitiveU64& recovery_pin);
