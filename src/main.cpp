// PNG Data Vehicle (pdvrdt v4.5). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023

#include "args.h"
#include "conceal.h"
#include "io_utils.h"
#include "recover.h"

int main(int argc, char** argv) {
	try {
		if (sodium_init() < 0) {
			throw std::runtime_error("Libsodium initialization failed!");
		}

#ifdef _WIN32
		SetConsoleOutputCP(CP_UTF8);
#endif

		auto args_opt = ProgramArgs::parse(argc, argv);
		if (!args_opt) return 0;

		auto& args = *args_opt;

		vBytes png_vec = readFile(
			args.image_file_path,
			args.mode == Mode::conceal ? FileTypeCheck::cover_image : FileTypeCheck::embedded_image
		);

		if (args.mode == Mode::conceal) {
			concealData(png_vec, args.option, args.data_file_path);
		} else {
			recoverData(png_vec);
		}
	}
	catch (const std::exception& e) {
		std::println(std::cerr, "\n{}\n", e.what());
		return 1;
	}

	return 0;
}
