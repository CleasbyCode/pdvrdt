#pragma once

#include "common.h"
#include "encryption.h"

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

// Decode the already validated pdvrdt cover to opaque RGB8 and calculate the
// exact adaptive (1,15,4) matrix-embedding carrier capacity. A stray IDAT
// appended by another tool is stripped from `png` in place.
[[nodiscard]] RedditPngCarrier prepareRedditPngCarrier(vBytes& png);

// Embed a raw payload using the PNGSTEG1 carrier format and encode a
// non-interlaced, metadata-free RGB PNG using carrier version 4, keyed by
// `carrier_keys.chacha` from deriveCarrierKeysFromPin() on a 128-bit KEY, which
// uses Argon2id.
[[nodiscard]] vBytes embedRedditPngPayload(
	RedditPngCarrier& carrier,
	const CarrierKeys& carrier_keys,
	std::span<const Byte> payload);

// Decode an untrusted image for carrier recovery. Needs no secret, so recover
// runs it before asking for the KEY/PIN: nullopt for an image that cannot hold a
// carrier (transparency, too few samples), and decode failures throw. A stray
// trailing IDAT is stripped from `png` in place.
[[nodiscard]] std::optional<RedditPngCarrier> decodeRedditRecoveryCarrier(vBytes& png);

// Return nullopt for a carrier that does not decode under `recovery_pin`, or for
// a CRC-valid generic PNGSTEG1 carrier that is not a pdvrdt profile. A 128-bit
// KEY tries only version 4. An old numeric PIN tries the version-3 and
// version-2 keys from one Argon2id derivation and, only if neither finds a
// header, the legacy version-1 key. Each key accepts only its
// own header version. Once a header is identified, structural or CRC failures
// are reported as corruption. KDF failures propagate without a legacy fallback.
[[nodiscard]] std::optional<vBytes> extractRedditPngPayload(
	const RedditPngCarrier& carrier,
	const RecoveryPin& recovery_pin);
