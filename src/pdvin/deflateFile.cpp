/* zlib.h -- interface of the 'zlib' general purpose compression library
  version 1.3.1, January 22nd, 2024

  Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler

  This software is provided 'as-is', without any express or implied
  warranty. In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Jean-loup Gailly        Mark Adler
  jloup@gzip.org          madler@alumni.caltech.edu
*/

#include "deflateFile.h"
#include <zlib.h>    
#include <algorithm> 
#include <utility>   

void deflateFile(std::vector<uint8_t>& vec, bool hasMastodonOption, bool isCompressedFile) {
	constexpr uint32_t 
		BUFSIZE = 2 * 1024 * 1024, 
		LARGE_FILE_SIZE	 = 500 * 1024 * 1024,  
		MEDIUM_FILE_SIZE = 200 * 1024 * 1024,  
		COMPRESSED_FILE_TYPE_SIZE_LIMIT = 50 * 1024 * 1024; 

	const uint32_t VEC_SIZE = static_cast<uint32_t>(vec.size());

	std::vector<uint8_t> buffer(BUFSIZE); 
    	std::vector<uint8_t> deflate_vec;
    	deflate_vec.reserve(VEC_SIZE + BUFSIZE);

    	z_stream strm = {};
    	strm.next_in = vec.data();
    	strm.avail_in = VEC_SIZE;
    	strm.next_out = buffer.data();
    	strm.avail_out = BUFSIZE;

	int8_t compression_level;

	if (isCompressedFile && VEC_SIZE > COMPRESSED_FILE_TYPE_SIZE_LIMIT) {
	    compression_level = Z_NO_COMPRESSION;
	} else if (VEC_SIZE > LARGE_FILE_SIZE) {
	    compression_level = Z_BEST_SPEED;
	} else if (VEC_SIZE > MEDIUM_FILE_SIZE || hasMastodonOption) {
	    compression_level = Z_DEFAULT_COMPRESSION;
	} else {
	    compression_level = Z_BEST_COMPRESSION;
	}

	deflateInit(&strm, compression_level);
	
	while (strm.avail_in > 0) {
		int ret = deflate(&strm, Z_NO_FLUSH);
        	if (ret != Z_OK) break;

        	if (strm.avail_out == 0) {
            		deflate_vec.insert(deflate_vec.end(), buffer.begin(), buffer.end());
            		strm.next_out = buffer.data();
            		strm.avail_out = BUFSIZE;
        	}
    	}

    	int ret;
    	do {
        	ret = deflate(&strm, Z_FINISH);
        	size_t bytes_written = BUFSIZE - strm.avail_out;
        	deflate_vec.insert(deflate_vec.end(), buffer.begin(), buffer.begin() + bytes_written);
        	strm.next_out = buffer.data();
        	strm.avail_out = BUFSIZE;
    	} while (ret == Z_OK);

    	deflateEnd(&strm);
    	vec = std::move(deflate_vec);
}
