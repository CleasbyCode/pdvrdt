cmake_minimum_required(VERSION 3.20)

project(pdvrdt VERSION 4.8 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "pdvrdt requires Linux.")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
  message(FATAL_ERROR "pdvrdt requires GCC 14 or newer.")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18)
  message(FATAL_ERROR "pdvrdt requires Clang 18 or newer with a compatible C++23 standard library.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PDVRDT_OPT_LEVEL "-O3" CACHE STRING "Optimization flag for pdvrdt")
set_property(CACHE PDVRDT_OPT_LEVEL PROPERTY STRINGS -O0 -O1 -O2 -O3 -Og -Os -Oz -Ofast)
if(NOT PDVRDT_OPT_LEVEL MATCHES "^-O(0|1|2|3|g|s|z|fast)$")
  message(FATAL_ERROR "Unsupported PDVRDT_OPT_LEVEL: ${PDVRDT_OPT_LEVEL}")
endif()

set(PDVRDT_FORTIFY_LEVEL "3" CACHE STRING "_FORTIFY_SOURCE level")
set_property(CACHE PDVRDT_FORTIFY_LEVEL PROPERTY STRINGS 2 3)
if(NOT PDVRDT_FORTIFY_LEVEL MATCHES "^[23]$")
  message(FATAL_ERROR "PDVRDT_FORTIFY_LEVEL must be 2 or 3.")
endif()

option(PDVRDT_ENABLE_LTO "Enable link-time optimization for the release binary" ON)

find_path(SODIUM_INCLUDE_DIR sodium.h REQUIRED)
find_library(SODIUM_LIBRARY NAMES sodium REQUIRED)
find_package(ZLIB REQUIRED)
find_path(LIBDEFLATE_INCLUDE_DIR libdeflate.h REQUIRED)
find_library(LIBDEFLATE_LIBRARY NAMES deflate REQUIRED)

add_executable(pdvrdt
  args.cpp
  compression.cpp
  conceal.cpp
  encryption.cpp
  image.cpp
  io_utils.cpp
  lodepng_crc32.cpp
  main.cpp
  png_utils.cpp
  recover.cpp
  lodepng/lodepng_build.cpp
)

target_include_directories(pdvrdt PRIVATE
  "${SODIUM_INCLUDE_DIR}"
  "${LIBDEFLATE_INCLUDE_DIR}"
)

target_compile_options(pdvrdt PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wconversion
  -Wformat
  -Wformat-security
  "${PDVRDT_OPT_LEVEL}"
  -march=native
  -pipe
  -fstack-protector-strong
  -fstack-clash-protection
  -fcf-protection=full
  -fPIE
  -ftrivial-auto-var-init=zero
)

target_compile_definitions(pdvrdt PRIVATE
  NDEBUG
  _GLIBCXX_ASSERTIONS
  "_FORTIFY_SOURCE=${PDVRDT_FORTIFY_LEVEL}"
)

target_link_options(pdvrdt PRIVATE
  -s
  -pie
  -Wl,-z,relro,-z,now,-z,noexecstack,-z,separate-code
)

target_link_libraries(pdvrdt PRIVATE
  "${SODIUM_LIBRARY}"
  ZLIB::ZLIB
  "${LIBDEFLATE_LIBRARY}"
)

if(PDVRDT_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT PDVRDT_IPO_SUPPORTED OUTPUT PDVRDT_IPO_ERROR LANGUAGES CXX)
  if(NOT PDVRDT_IPO_SUPPORTED)
    message(FATAL_ERROR "The selected compiler does not support LTO: ${PDVRDT_IPO_ERROR}")
  endif()
  set_property(TARGET pdvrdt PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
