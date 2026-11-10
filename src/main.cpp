// PNG Data Vehicle (pdvrdt). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023

#include "args.h"
#include "conceal.h"
#include "io_utils.h"
#include "process_hardening.h"
#include "recover.h"

#include <iostream>
#include <print>
#include <stdexcept>

int main(int argc, char** argv) {
	disableCoreDumps();
	try {
		if (sodium_init() < 0) {
			throw std::runtime_error("Libsodium initialization failed!");
		}

		auto args_opt = ProgramArgs::parse(argc, argv);
		if (!args_opt) return 0;

		auto& args = *args_opt;

		const FileTypeCheck image_check = args.mode == Mode::recover
			? FileTypeCheck::embedded_image
			: (args.mode == Mode::capsize || args.option == Option::Reddit
				? FileTypeCheck::reddit_cover_image
				: FileTypeCheck::cover_image);
		if (args.mode == Mode::recover) {
			recoverDataFromFile(args.image_file_path);
			return 0;
		}
		vBytes png_vec = readFile(args.image_file_path, image_check);

		if (args.mode == Mode::conceal) {
			concealData(png_vec, args.option, args.image_file_path, args.data_file_path);
		} else {
			displayRedditCapacity(png_vec, args.image_file_path);
		}
	}
	catch (const std::exception& e) {
		std::println(std::cerr, "\n{}\n", e.what());
		return 1;
	}
	return 0;
}
