//	PNG Data Vehicle for Reddit, (PDVRDT v1.0). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023

#include <fstream>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

unsigned long updateCrc(const unsigned long&, unsigned char*, const size_t&);
unsigned long crc(unsigned char*, const size_t&);
void processFiles(char* []);
void processEmbeddedImage(char* []);
void readFilesIntoVectors(std::ifstream&, std::ifstream&, const std::string&, const std::string&, const ptrdiff_t&, const ptrdiff_t&);
void eraseChunks(std::vector<unsigned char>&);
void writeFile(std::vector<unsigned char>&, const std::string&);
void insertValue(std::vector<unsigned char>&, ptrdiff_t, const size_t&, int);
void displayInfo();

const std::string
	ICCP_ID = "iCCPICC\x20Profile",
	PDV_ID = "\x20PDVRDT\x20",
	PNG_ID = "\x89PNG",
	EMBEDDED_FILE = "pdvrdt_image.png",
	READ_ERR_MSG = "\nRead Error: Unable to open/read file: ",
	TMP_COMPRESSED_FILE = "PDVRDT_PROFILE_TMP.z",
	TMP_UNCOMPRESSED_FILE = "PDVRDT_PROFILE_TMP",
	ZLIB_UNCOMPRESS_CMD = "zlib-flate -uncompress < " + TMP_COMPRESSED_FILE + " > " + TMP_UNCOMPRESSED_FILE,
	ZLIB_COMPRESS_CMD = "zlib-flate -compress < " + TMP_UNCOMPRESSED_FILE + " > " + TMP_COMPRESSED_FILE;

int main(int argc, char** argv) {

	if (argc == 2 && std::string(argv[1]) == "--info") {
		displayInfo();
	}
	else if (argc < 2 || argc > 3) {
		std::cerr << "\nUsage:-\n\tInsert:  pdvrdt  <png_image>  <your_file>\n\tExtract: pdvrdt  <png_image>\n\tHelp:\t pdvrdt  --info\n\n";
	}
	else if (argc == 3) {
		processFiles(argv);
	}
	else {
		processEmbeddedImage(argv);
	}
	return 0;
}

void processFiles(char* argv[]) {

	const std::string
		IMG_FILE = argv[1],
		DATA_FILE = argv[2];

	std::ifstream
		readImg(IMG_FILE, std::ios::binary),
		readFile(DATA_FILE, std::ios::binary);

	if (!readImg || !readFile) {

		const std::string ERR_MSG = !readImg ? READ_ERR_MSG + "'" + IMG_FILE + "'\n\n" : READ_ERR_MSG + "'" + DATA_FILE + "'\n\n";
		std::cerr << ERR_MSG;
		std::exit(EXIT_FAILURE);
	}

	const int
		MAX_PNG_SIZE_BYTES = 19922944,		
		MAX_DATAFILE_SIZE_BYTES = 1048444;	
					
	readImg.seekg(0, readImg.end),
	readFile.seekg(0, readFile.end);

	const ptrdiff_t
		IMG_SIZE = readImg.tellg(),
		DATA_SIZE = readFile.tellg();

	if ((IMG_SIZE + MAX_DATAFILE_SIZE_BYTES) > MAX_PNG_SIZE_BYTES
		|| DATA_SIZE > MAX_DATAFILE_SIZE_BYTES) {

		const std::string
			SIZE_ERR_PNG = "\nImage Size Error: PNG image must not exceed Reddit's file size limit of 19MB.\n\n",
			SIZE_ERR_DATA = "\nFile Size Error: Your data file must not exceed 1,048,444 bytes.\n\n",

		ERR_MSG = IMG_SIZE + MAX_DATAFILE_SIZE_BYTES > MAX_PNG_SIZE_BYTES ? SIZE_ERR_PNG :  SIZE_ERR_DATA;

		std::cerr << ERR_MSG;
		std::exit(EXIT_FAILURE);
	}

	readFilesIntoVectors(readImg, readFile, IMG_FILE, DATA_FILE, IMG_SIZE, DATA_SIZE);
}

void processEmbeddedImage(char* argv[]) {

	const std::string IMG_FILE = argv[1];

	std::ifstream readImg(IMG_FILE, std::ios::binary);

	if (!readImg) {
		std::cerr << READ_ERR_MSG + IMG_FILE + "'\n\n";
		std::exit(EXIT_FAILURE);
	}

	std::vector<unsigned char> image_file_vec((std::istreambuf_iterator<char>(readImg)), std::istreambuf_iterator<char>());

	const std::string ICCP_CHECK{ image_file_vec.begin() + 37, image_file_vec.begin() + 37 + ICCP_ID.length() };

	if (ICCP_CHECK != ICCP_ID) {
		std::cerr << "\nPNG Error: Image file does not appear to contain a valid iCCP profile.\n\n";
		std::exit(EXIT_FAILURE);
	}

	int
	iccpLengthIndex = 33, 
	iccpChunkLength = (image_file_vec[iccpLengthIndex + 1] << 16) | image_file_vec[iccpLengthIndex + 2] << 8 | image_file_vec[iccpLengthIndex + 3], 
	zlibChunkIndex = 54, 
	zlibChunkSize = iccpChunkLength - 13; 

	image_file_vec.erase(image_file_vec.begin(), image_file_vec.begin() + zlibChunkIndex);
	image_file_vec.erase(image_file_vec.begin() + zlibChunkSize, image_file_vec.end());

	writeFile(image_file_vec, TMP_COMPRESSED_FILE);
  
	system(ZLIB_UNCOMPRESS_CMD.c_str());

	std::ifstream readTmp(TMP_UNCOMPRESSED_FILE, std::ios::binary);

	if (!readTmp) {

		std::cerr << READ_ERR_MSG + TMP_UNCOMPRESSED_FILE + "\n\n";
		std::exit(EXIT_FAILURE);
	}

	std::vector<unsigned char> tmp_file_vec((std::istreambuf_iterator<char>(readTmp)), std::istreambuf_iterator<char>());

	const std::string
		PDV_CHECK{ tmp_file_vec.begin() + 4, tmp_file_vec.begin() + 4 + PDV_ID.length() },	
		FILE_EXT = { tmp_file_vec.begin() + 13, tmp_file_vec.begin() + 13 + tmp_file_vec[12] }, 
		EXTRACTED_FILE = "pdvrdt_extracted_file" + FILE_EXT;

	if (PDV_CHECK != PDV_ID) {
		std::cerr << "\nProfile Error: iCCP Profile does not seem to be a PDVRDT modified profile.\n\n";
		std::exit(EXIT_FAILURE);
	}
  
	tmp_file_vec.erase(tmp_file_vec.begin(), tmp_file_vec.begin() + 132);

	readTmp.close();
  
	remove(TMP_COMPRESSED_FILE.c_str());
	remove(TMP_UNCOMPRESSED_FILE.c_str());

	writeFile(tmp_file_vec, EXTRACTED_FILE);

	std::cout << "\nAll done!\n\nCreated output file: '" + EXTRACTED_FILE + "'\n\n";

}

void readFilesIntoVectors(std::ifstream& readImg, std::ifstream& readFile, const std::string& IMG_FILE, const std::string& DATA_FILE, const ptrdiff_t& IMG_SIZE, const ptrdiff_t& DATA_SIZE) {
  
	readImg.seekg(0, readImg.beg),
	readFile.seekg(0, readFile.beg);
  
	std::vector<unsigned char>
		iccp_file_vec{
			0x00, 0x00, 0x00, 0x84, 0x20, 0x50, 0x44, 0x56, 0x52, 0x44, 0x54, 0x20,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x61, 0x63, 0x73, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	},
  
		iccp_chunk_vec
	{
			0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50, 0x49, 0x43, 0x43, 0x20,
			0x50, 0x72, 0x6F, 0x66, 0x69, 0x6C, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00 },
  
		image_file_vec((std::istreambuf_iterator<char>(readImg)), std::istreambuf_iterator<char>());

	iccp_file_vec.resize(DATA_SIZE + iccp_file_vec.size() / sizeof(unsigned char));
	readFile.read((char*)&iccp_file_vec[132], DATA_SIZE);

	const std::string
		PNG_CHECK{ image_file_vec.begin(), image_file_vec.begin() + PNG_ID.length() };
		
	if (PNG_CHECK != PNG_ID) {
		std::cerr << "\nImage Error: File does not appear to be a valid PNG image.\n";
		std::exit(EXIT_FAILURE);
	}
  
	eraseChunks(image_file_vec);

	ptrdiff_t 
		dot = DATA_FILE.find_last_of('.'),	
		iccpChunkLengthIndex = 1,		
		iccpFileExtensionLengthIndex = 12,	
		iccpFileExtensionIndex = 13,		
		dataFileNameLength = DATA_FILE.length(); 
	
	std::string file_extension;
	
	if (dot < 0 || dataFileNameLength - dot == 1) {
		file_extension = ".txt";
	} else {
		file_extension = dataFileNameLength - dot < 5 ? DATA_FILE.substr(dot, (dataFileNameLength - dot)) : DATA_FILE.substr(dot, 5);
	}

	insertValue(iccp_file_vec, iccpFileExtensionLengthIndex, file_extension.length(), 8);

	iccp_file_vec.erase(iccp_file_vec.begin() + iccpFileExtensionIndex, iccp_file_vec.begin() + file_extension.length() + iccpFileExtensionIndex);
	iccp_file_vec.insert(iccp_file_vec.begin() + iccpFileExtensionIndex, file_extension.begin(), file_extension.end());
	
	writeFile(iccp_file_vec, TMP_UNCOMPRESSED_FILE);

	system(ZLIB_COMPRESS_CMD.c_str());

	std::ifstream readProfile(TMP_COMPRESSED_FILE, std::ios::binary);

	if (!readProfile) {
		std::cerr << READ_ERR_MSG + TMP_COMPRESSED_FILE;
		std::exit(EXIT_FAILURE);
	}

	readProfile.seekg(0, readProfile.end);

	const ptrdiff_t
		PROFILE_SIZE = readProfile.tellg(),
		PROFILE_CHUNK_SIZE = PROFILE_SIZE + 17,
		ICCP_CHUNK_INSERT_INDEX = 33; 
	
	readProfile.seekg(0, readProfile.beg);

	iccp_chunk_vec.resize(PROFILE_SIZE + iccp_chunk_vec.size() / sizeof(unsigned char));
	readProfile.read((char*)&iccp_chunk_vec[21], PROFILE_SIZE);

	readProfile.close();
  
	insertValue(iccp_chunk_vec, iccpChunkLengthIndex, PROFILE_SIZE + 13, 24);
	
	const int ICCP_CHUNK_START_INDEX = 4;
	const uint32_t ICCP_CHUNK_CRC = crc(&iccp_chunk_vec[ICCP_CHUNK_START_INDEX], PROFILE_CHUNK_SIZE);

	ptrdiff_t iCCPInsertIndexCrc = ICCP_CHUNK_START_INDEX + (PROFILE_CHUNK_SIZE); 

	insertValue(iccp_chunk_vec, iCCPInsertIndexCrc, ICCP_CHUNK_CRC, 32);
	image_file_vec.insert((image_file_vec.begin() + ICCP_CHUNK_INSERT_INDEX), iccp_chunk_vec.begin(), iccp_chunk_vec.end());

	writeFile(image_file_vec, EMBEDDED_FILE);
  
	remove(TMP_COMPRESSED_FILE.c_str());
	remove(TMP_UNCOMPRESSED_FILE.c_str());

	std::cout << "\nAll done!\n\nCreated output file: '"+EMBEDDED_FILE+"'\nYou can now upload and share this PNG image on Reddit.\n\n";

}

void eraseChunks(std::vector<unsigned char>& image_file_vec) {

	const std::string CHUNK[15]{ "IDAT", "bKGD", "cHRM", "sRGB", "hIST", "iCCP", "pHYs", "sBIT", "gAMA", "sPLT", "tIME", "tRNS", "tEXt", "iTXt", "zTXt" };
	
	for (int chunkIndex = 14; chunkIndex > 0; chunkIndex--) {
		ptrdiff_t
			firstIdatIndex = search(image_file_vec.begin(), image_file_vec.end(), CHUNK[0].begin(), CHUNK[0].end()) - image_file_vec.begin(),
			chunkFoundIndex = search(image_file_vec.begin(), image_file_vec.end(), CHUNK[chunkIndex].begin(), CHUNK[chunkIndex].end()) - image_file_vec.begin() - 4;
		if (firstIdatIndex > chunkFoundIndex) {
			int chunkSize = (image_file_vec[chunkFoundIndex + 1] << 16) | image_file_vec[chunkFoundIndex + 2] << 8 | image_file_vec[chunkFoundIndex + 3];
			image_file_vec.erase(image_file_vec.begin() + chunkFoundIndex, image_file_vec.begin() + chunkFoundIndex + (chunkSize + 12));
			chunkIndex++;
		}
	}
}

unsigned long updateCrc(const unsigned long& crc, unsigned char* buf, const size_t& len)
{

	unsigned long crcTable[256]{ 0,1996959894,3993919788,2567524794,124634137,1886057615,3915621685,2657392035,249268274,2044508324,3772115230,2547177864,162941995,
					2125561021,3887607047,2428444049,498536548,1789927666,4089016648,2227061214,450548861,1843258603,4107580753,2211677639,325883990,
					1684777152,4251122042,2321926636,335633487,1661365465,4195302755,2366115317,997073096,1281953886,3579855332,2724688242,1006888145,
					1258607687,3524101629,2768942443,901097722,1119000684,3686517206,2898065728,853044451,1172266101,3705015759,2882616665,651767980,
					1373503546,3369554304,3218104598,565507253,1454621731,3485111705,3099436303,671266974,1594198024,3322730930,2970347812,795835527,
					1483230225,3244367275,3060149565,1994146192,31158534,2563907772,4023717930,1907459465,112637215,2680153253,3904427059,2013776290,
					251722036,2517215374,3775830040,2137656763,141376813,2439277719,3865271297,1802195444,476864866,2238001368,4066508878,1812370925,
					453092731,2181625025,4111451223,1706088902,314042704,2344532202,4240017532,1658658271,366619977,2362670323,4224994405,1303535960,
					984961486,2747007092,3569037538,1256170817,1037604311,2765210733,3554079995,1131014506,879679996,2909243462,3663771856,1141124467,
					855842277,2852801631,3708648649,1342533948,654459306,3188396048,3373015174,1466479909,544179635,3110523913,3462522015,1591671054,
					702138776,2966460450,3352799412,1504918807,783551873,3082640443,3233442989,3988292384,2596254646,62317068,1957810842,3939845945,
					2647816111,81470997,1943803523,3814918930,2489596804,225274430,2053790376,3826175755,2466906013,167816743,2097651377,4027552580,
					2265490386,503444072,1762050814,4150417245,2154129355,426522225,1852507879,4275313526,2312317920,282753626,1742555852,4189708143,
					2394877945,397917763,1622183637,3604390888,2714866558,953729732,1340076626,3518719985,2797360999,1068828381,1219638859,3624741850,
					2936675148,906185462,1090812512,3747672003,2825379669,829329135,1181335161,3412177804,3160834842,628085408,1382605366,3423369109,
					3138078467,570562233,1426400815,3317316542,2998733608,733239954,1555261956,3268935591,3050360625,752459403,1541320221,2607071920,
					3965973030,1969922972,40735498,2617837225,3943577151,1913087877,83908371,2512341634,3803740692,2075208622,213261112,2463272603,
					3855990285,2094854071,198958881,2262029012,4057260610,1759359992,534414190,2176718541,4139329115,1873836001,414664567,2282248934,
					4279200368,1711684554,285281116,2405801727,4167216745,1634467795,376229701,2685067896,3608007406,1308918612,956543938,2808555105,
					3495958263,1231636301,1047427035,2932959818,3654703836,1088359270,936918000,2847714899,3736837829,1202900863,817233897,3183342108,
					3401237130,1404277552,615818150,3134207493,3453421203,1423857449,601450431,3009837614,3294710456,1567103746,711928724,3020668471,
					3272380065,1510334235,755167117 };

	unsigned long c = crc;

	for (size_t n{}; n < len; n++) {
		c = crcTable[(c ^ buf[n]) & 0xff] ^ (c >> 8);
	}
	return c;
}

unsigned long crc(unsigned char* buf, const size_t& len)
{
	return updateCrc(0xffffffffL, buf, len) ^ 0xffffffffL;
}

void writeFile(std::vector<unsigned char>&vec, const std::string& OUT_FILE) {

	std::ofstream writeFile(OUT_FILE, std::ios::binary);

	if (!writeFile) {
		std::cerr << "\nWrite Error: Unable to write to file.\n\n";
		std::exit(EXIT_FAILURE);
	}

	writeFile.write((char*)&vec[0], vec.size());
	writeFile.close();
}

void insertValue(std::vector<unsigned char>& vec, ptrdiff_t valueInsertIndex, const size_t& VALUE, int bits) {

		while (bits) vec[valueInsertIndex++] = (VALUE >> (bits -= 8)) & 0xff;
}

void displayInfo() {

	std::cout << R"(
PNG Data Vehicle for Reddit, (PDVRDT v1.0). Created by Nicholas Cleasby (@CleasbyCode) 24/01/2023.

PDVRDT enables you to embed & extract arbitrary data of upto ~1MB within a PNG image.
You can then upload and share your data embedded image file on Reddit. 

PDVRDT data embedded images will not work with Twitter. For Twitter, please use PDVZIP.

This program works on Linux and Windows.

PDVRDT currently requires the external program 'zlib-flate'.

If not already installed, you can install it for Linux with 'apt install qpdf'.
For Windows you can download the installer from Sourceforge. (https://sourceforge.net/projects/qpdf/).
 
1,048,444 bytes is the (zlib) uncompressed limit for your arbitrary data.
132 bytes is used for the barebones iCCP profile. (132 + 1048444 = 1,048,576 / 1MB).

To maximise the amount of data you can embed in your image file, I recommend compressing your 
data file(s) to zip, rar, etc. Make sure the zip/rar compressed file does not exceed 1,048,444 bytes.

Your file will be compressed again in zlib format when embedded into the image file (iCCP profile chunk).

)";
}
