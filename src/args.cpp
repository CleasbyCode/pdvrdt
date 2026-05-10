#include "args.h"

#include <format>
#include <print>
#include <stdexcept>
#include <string_view>

namespace {

void displayInfo() {
	std::print(R"(

PNG Data Vehicle (pdvrdt v4.7)
Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

pdvrdt is a metadata "steganography-like" command-line tool used for concealing and extracting
any file type within and from a PNG image.

──────────────────────────
Compile & run (Linux)
──────────────────────────

  $ sudo apt-get install libsodium-dev

  $ chmod +x compile_pdvrdt.sh
  $ ./compile_pdvrdt.sh

  Compilation successful. Executable 'pdvrdt' created.

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

Share your "file-embedded" PNG image on the following compatible sites.

Size limit is measured by the combined size of cover image + compressed data file:

	• Flickr    (200 MB)
	• ImgBB     (32 MB)
	• PostImage (32 MB)
	• Reddit    (19 MB) — (use -r option).
	• Mastodon  (16 MB) — (use -m option).
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
Platform options for conceal mode
──────────────────────────

  -m (Mastodon) : Creates compatible "file-embedded" PNG images for posting on Mastodon.

      $ pdvrdt conceal -m my_image.png hidden.doc
 
  -r (Reddit) : Creates compatible "file-embedded" PNG images for posting on Reddit.

      $ pdvrdt conceal -r my_image.png secret.mp3

    From the Reddit site, click "Create Post", then select the "Images & Video" tab to attach the PNG image.
    These images are only compatible for posting on Reddit.

──────────────────────────
Notes
──────────────────────────

• To correctly download images from X-Twitter or Reddit, click image within the post to fully expand it before saving.
• ImgPile: sign in to an account before sharing; otherwise, the embedded data will not be preserved.

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
	const std::string_view platform_option = argAt(argc, argv, i);
	if (platform_option == "-m" || platform_option == "-r") {
		out.option = (platform_option == "-m") ? Option::Mastodon : Option::Reddit;
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
