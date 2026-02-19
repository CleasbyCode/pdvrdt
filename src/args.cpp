#include "args.h"

void displayInfo() {
	std::print(R"(

PNG Data Vehicle (pdvrdt v4.5)
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

  -m (Mastodon) : Createa compatible "file-embedded" PNG images for posting on Mastodon.

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

void ProgramArgs::die(const std::string& usage) {
	throw std::runtime_error(usage);
}

std::optional<ProgramArgs> ProgramArgs::parse(int argc, char** argv) {
	const auto arg = [&](int i) -> std::string_view {
		return (i >= 0 && i < argc) ? argv[i] : std::string_view{};
	};

	const std::string
		prog = fs::path(argv[0]).filename().string(),
		usage = std::format(
		"Usage: {} conceal [-m|-r] <cover_image> <secret_file>\n"
		"       {} recover <cover_image>\n"
		"       {} --info",
		prog, prog, prog
	);

	if (argc < 2) die(usage);

	if (argc == 2 && arg(1) == "--info") {
		displayInfo();
		return std::nullopt;
	}

	const std::string_view mode = arg(1);

	if (mode == "conceal") {
		ProgramArgs out{};
		out.mode = Mode::conceal;
		int i = 2;

		if (arg(i) == "-m" || arg(i) == "-r") {
			out.option = (arg(i) == "-m") ? Option::Mastodon : Option::Reddit;
			++i;
		}

		if (argc != i + 2 || arg(i).empty() || arg(i + 1).empty()) die(usage);

		out.image_file_path = arg(i);
		out.data_file_path = arg(i + 1);
		return out;
	}

	if (mode == "recover") {
		if (argc != 3 || arg(2).empty()) die(usage);

		ProgramArgs out{};
		out.mode = Mode::recover;
		out.image_file_path = fs::path(arg(2));
		return out;
	}

	die(usage);
}
