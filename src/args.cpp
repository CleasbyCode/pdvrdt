#include "args.h"
#include "io_utils.h"

#include <format>
#include <print>
#include <stdexcept>
#include <string_view>

namespace {

// The limits quoted in displayInfo() below are prose, so pin them to the
// constants they describe: the help text cannot silently drift out of date.
static_assert(MAX_COVER_IMAGE_SIZE == 8ULL * 1024 * 1024,
	"displayInfo() quotes an 8 MB cover-image limit; keep it in step with MAX_COVER_IMAGE_SIZE.");
static_assert(REDDIT_UPLOAD_SIZE_LIMIT == 20ULL * 1024 * 1024,
	"displayInfo() quotes a 20 MiB Reddit input limit; keep it in step with REDDIT_UPLOAD_SIZE_LIMIT.");

void displayInfo() {
	std::print("\n\nPNG Data Vehicle (pdvrdt v{})\n", PDVRDT_VERSION);
	std::print(R"(Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

pdvrdt is a "steganography-like" command-line tool used for concealing and extracting
any file type within and from a PNG image. Default and Mastodon modes carry the payload
in PNG metadata chunks; Reddit mode carries it in the image pixels themselves.

──────────────────────────
Compile & run (Linux)
──────────────────────────

  $ sudo apt update
  $ sudo apt install g++ cmake ninja-build util-linux libsodium-dev zlib1g-dev libdeflate-dev

  $ chmod +x compile_pdvrdt.sh
  $ ./compile_pdvrdt.sh

  $ sudo cp pdvrdt /usr/bin
  $ pdvrdt

──────────────────────────
Usage
──────────────────────────

  pdvrdt conceal [-m|-r] <cover_image> <secret_file>
  pdvrdt recover <cover_image>
  pdvrdt --info

──────────────────────────
Platform compatibility & size limits
──────────────────────────

Input limit: the PNG cover image must normally be 8 MiB or smaller. Reddit mode
instead permits a cover up to 20 MiB and also limits its payload input to 20 MiB.
Other modes have no separate payload-input limit; their output limit applies
after compression.

Share your "file-embedded" PNG image on the following compatible sites.

Except for Reddit's adaptive in-pixel carrier, size limits are measured by the
combined size of cover image + compressed data file:

The original file may be larger than a platform limit. The limit is applied
after compression, so a larger, highly compressible input can still succeed.

	• Flickr    (200 MB)
	• ImgBB     (32 MB)
	• PostImage (32 MB)
	• Mastodon  (16 MiB) — (use -m option).
	• Reddit    (20 MiB input limit per cover and payload) — (use -r option).
	• ImgPile   (8 MB)
	• X-Twitter (5 MB)  — (*Dimension size limits).

X-Twitter Image Dimension Size Limits:

	• PNG-32/24 (Truecolor) 68x68 Min. <-> 900x900 Max.
	• PNG-8 (Indexed-color) 68x68 Min. <-> 4096x4096 Max.

──────────────────────────
Modes
──────────────────────────

  conceal - Compresses, encrypts and embeds your secret data file within a PNG cover image.
  recover - Decrypts, uncompresses and extracts the concealed data file from a PNG cover image
            (recovery PIN required).

──────────────────────────
Platform option for conceal mode
──────────────────────────

  -m (Mastodon) : Creates compatible "file-embedded" PNG images for posting on Mastodon.

      $ pdvrdt conceal -m my_image.png hidden.doc

  -r (Reddit) : Creates an adaptive spatial "file-embedded" RGB PNG for Reddit.

      $ pdvrdt conceal -r my_image.png hidden.doc

      Method: content-adaptive spatial embedding in the pixel LSBs. Each 4 bits of
      payload is carried by 15 RGB samples drawn from a permutation keyed by the
      recovery PIN, using (1,15,4) Hamming-syndrome matrix embedding: the syndrome
      is moved to the target by a single +/-1 change (LSB matching, not LSB
      replacement). Which sample to change is chosen by a per-sample distortion
      cost from local image activity and channel sensitivity, so edits land in
      textured regions rather than flat ones. Where two changes cost less than the
      one required change, it takes them -- measured on a 4.3-megapixel cover, 93%
      of groups need one change, 0.4% take two and 6% need none, about 4.25 bits
      per changed sample.

      Because the sample positions come from the PIN, an image reveals nothing
      without it: a wrong PIN and an ordinary PNG are indistinguishable.

      Reddit cover and payload input files must each be no larger than 20 MiB.
      The payload is compressed before its carrier-capacity check.

──────────────────────────
Notes
──────────────────────────

• To correctly download images from X-Twitter, click image within the post to fully expand it before saving.
• ImgPile: sign in to an account before sharing; otherwise, the embedded data will not be preserved.
• Animated PNG (APNG) covers are not supported. Static PNG color metadata is preserved.
• Mastodon mode accepts iCCP/sRGB covers. Because its payload uses iCCP, those profile declarations are replaced; compatible color metadata is retained.
• Reddit mode emits a non-interlaced, metadata-free, 8-bit RGB PNG. Covers containing transparency are not supported.
• Default and Mastodon modes carry the payload in a PNG chunk. That a chunk is present is visible to
  anyone; what is hidden is its contents. Reddit mode instead derives its carrier positions from the
  recovery PIN, so without the PIN there is nothing to locate.

)");
}

[[nodiscard]] std::string_view argAt(int argc, char** argv, int i) {
	if (i < 0 || i >= argc) {
		return {};
	}
	const char* const s = argv[i];
	return s ? std::string_view{s} : std::string_view{};
}

[[nodiscard]] std::string programName(int argc, char** argv) {
	// POSIX permits argc == 0 and argv[0] == nullptr; fall back to the binary name.
	if (argc >= 1 && argv[0] != nullptr) {
		return fs::path(argv[0]).filename().string();
	}
	return "pdvrdt";
}

[[nodiscard]] std::string buildUsage(std::string_view prog) {
	return std::format(
		"Usage: {} conceal [-m|-r] <cover_image> <secret_file>\n"
		"       {} recover <cover_image>\n"
		"       {} --info",
		prog, prog, prog
	);
}

[[noreturn]] void dieUsage(const std::string& usage) {
	throw std::runtime_error(usage);
}

[[nodiscard]] ProgramArgs parseConcealArgs(int argc, char** argv, const std::string& usage) {
	ProgramArgs out{};
	out.mode = Mode::conceal;

	int i = 2;
	if (argAt(argc, argv, i) == "-m" || argAt(argc, argv, i) == "-r") {
		out.option = argAt(argc, argv, i) == "-m" ? Option::Mastodon : Option::Reddit;
		++i;
	}

	if (argc != i + 2 || argAt(argc, argv, i).empty() || argAt(argc, argv, i + 1).empty()) {
		dieUsage(usage);
	}

	out.image_file_path = argAt(argc, argv, i);
	out.data_file_path = argAt(argc, argv, i + 1);
	return out;
}

[[nodiscard]] ProgramArgs parseRecoverArgs(int argc, char** argv, const std::string& usage) {
	if (argc != 3 || argAt(argc, argv, 2).empty()) {
		dieUsage(usage);
	}

	ProgramArgs out{};
	out.mode = Mode::recover;
	out.image_file_path = fs::path(argAt(argc, argv, 2));
	return out;
}

} // namespace

std::optional<ProgramArgs> ProgramArgs::parse(int argc, char** argv) {
	const std::string usage = buildUsage(programName(argc, argv));

	if (argc < 2) {
		dieUsage(usage);
	}

	const std::string_view mode = argAt(argc, argv, 1);
	if (argc == 2 && mode == "--info") {
		displayInfo();
		return std::nullopt;
	}

	if (mode == "conceal") return parseConcealArgs(argc, argv, usage);
	if (mode == "recover") return parseRecoverArgs(argc, argv, usage);

	dieUsage(usage);
}
