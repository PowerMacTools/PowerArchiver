# Builds libarchive and the compression/crypto libraries it uses, all from
# submodules and statically linked, so nothing comes from the system. This is
# needed for targets like classic Mac OS. Produces `archive_static`.
#
# There's no XML library, so xar isn't supported.
#
# libarchive normally finds its dependencies with configure-time checks that
# link against already-installed libraries. Those can't see targets we build
# here, so all of its dependency detection is disabled, and each library is
# instead switched on directly with the defines its config.h would have set.

set(SUBMODULES ${CMAKE_CURRENT_SOURCE_DIR}/../../submodules)
set(BUILD_SHARED_LIBS OFF)

# zlib: gzip, zip/7z/cab deflate, zisofs.
set(ZLIB_DIR ${SUBMODULES}/zlib)
add_library(zlib STATIC
  ${ZLIB_DIR}/adler32.c ${ZLIB_DIR}/compress.c ${ZLIB_DIR}/crc32.c
  ${ZLIB_DIR}/deflate.c ${ZLIB_DIR}/gzclose.c ${ZLIB_DIR}/gzlib.c
  ${ZLIB_DIR}/gzread.c ${ZLIB_DIR}/gzwrite.c ${ZLIB_DIR}/infback.c
  ${ZLIB_DIR}/inffast.c ${ZLIB_DIR}/inflate.c ${ZLIB_DIR}/inftrees.c
  ${ZLIB_DIR}/trees.c ${ZLIB_DIR}/uncompr.c ${ZLIB_DIR}/zutil.c)
target_include_directories(zlib PUBLIC ${ZLIB_DIR})
# Milsko compiles its own copy of zlib into Mw; prefix ours (z_inflate, ...)
# so the two can't collide in the static link.
target_compile_definitions(zlib PUBLIC Z_PREFIX)
# zconf.h only includes unistd.h (for lseek, off_t) when this is set; zlib's
# own build checks for it the same way. Same value as libarchive's config.h.
include(CheckIncludeFile)
check_include_file(unistd.h ZLIB_HAVE_UNISTD_H)
if(ZLIB_HAVE_UNISTD_H)
  target_compile_definitions(zlib PUBLIC HAVE_UNISTD_H=1)
endif()

# bzip2: bzip2, zip/7z bzip2.
set(BZIP2_DIR ${SUBMODULES}/bzip2)
add_library(bz2 STATIC
  ${BZIP2_DIR}/blocksort.c ${BZIP2_DIR}/huffman.c ${BZIP2_DIR}/crctable.c
  ${BZIP2_DIR}/randtable.c ${BZIP2_DIR}/compress.c ${BZIP2_DIR}/decompress.c
  ${BZIP2_DIR}/bzlib.c)
target_include_directories(bz2 PUBLIC ${BZIP2_DIR})

# liblzma: xz, lzma, lzip, 7z/zip LZMA and LZMA2. 7z needs it even for archives
# using other methods, since its headers are LZMA-compressed.
set(XZ_TOOL_XZ OFF)
set(XZ_TOOL_XZDEC OFF)
set(XZ_TOOL_LZMADEC OFF)
set(XZ_TOOL_LZMAINFO OFF)
set(XZ_TOOL_SCRIPTS OFF)
set(XZ_DOC OFF)
set(XZ_NLS OFF)
set(XZ_THREADS no CACHE STRING "" FORCE)
if(CMAKE_SYSTEM_NAME MATCHES Retro)
  # Classic Mac OS has no way for tuklib to count cores or memory, which xz
  # treats as a configure error. Claim they were found with no definitions so
  # lzma_cputhreads() and lzma_physmem() just report 0.
  set(TUKLIB_CPUCORES_FOUND 1 CACHE INTERNAL "")
  set(TUKLIB_PHYSMEM_FOUND 1 CACHE INTERNAL "")
endif()
add_subdirectory(${SUBMODULES}/xz ${CMAKE_CURRENT_BINARY_DIR}/xz
  EXCLUDE_FROM_ALL)
# xz only exports its headers through its install rules, so without this
# libarchive silently picks up the system's lzma.h instead
target_include_directories(liblzma INTERFACE
  $<BUILD_INTERFACE:${SUBMODULES}/xz/src/liblzma/api>)

# zstd: zstd, zip/7z zstd.
set(ZSTD_DIR ${SUBMODULES}/zstd/lib)
file(GLOB zstd_sources ${ZSTD_DIR}/common/*.c ${ZSTD_DIR}/compress/*.c
  ${ZSTD_DIR}/decompress/*.c)
add_library(zstd STATIC ${zstd_sources})
target_include_directories(zstd PUBLIC ${ZSTD_DIR})
# the x86-64 assembly decoder is optional; keep everything portable C
target_compile_definitions(zstd PRIVATE ZSTD_DISABLE_ASM)

# lz4: lz4.
set(LZ4_DIR ${SUBMODULES}/lz4/lib)
add_library(lz4 STATIC ${LZ4_DIR}/lz4.c ${LZ4_DIR}/lz4hc.c
  ${LZ4_DIR}/lz4frame.c ${LZ4_DIR}/xxhash.c)
target_include_directories(lz4 PUBLIC ${LZ4_DIR})
# zstd also bundles xxhash, so namespace lz4's copy like lz4's own build does
target_compile_definitions(lz4 PRIVATE XXH_NAMESPACE=LZ4_)

# mbed TLS: encrypted zips. Its own CMake needs the separate
# mbedtls-framework repo, so build the library sources directly; the config
# compiles out every module libarchive doesn't use.
set(MBEDTLS_DIR ${SUBMODULES}/mbedtls)
file(GLOB mbedcrypto_sources ${MBEDTLS_DIR}/library/*.c)
add_library(mbedcrypto STATIC ${mbedcrypto_sources})
target_include_directories(mbedcrypto
  PUBLIC ${MBEDTLS_DIR}/include
  PRIVATE ${MBEDTLS_DIR}/library)
target_compile_definitions(mbedcrypto PUBLIC
  MBEDTLS_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/mbedtls_config.h")

# libarchive itself. Skip its CLI tools, tests and install rules;
# EXCLUDE_FROM_ALL keeps everything but what we link out of the build.
set(ENABLE_TAR OFF)
set(ENABLE_CPIO OFF)
set(ENABLE_CAT OFF)
set(ENABLE_UNZIP OFF)
set(ENABLE_TEST OFF)
set(ENABLE_INSTALL OFF)
# warnings in vendored code shouldn't break the build
set(ENABLE_WERROR OFF)
foreach(dep MBEDTLS NETTLE OPENSSL MD LIBB2 LZ4 LZO LZMA ZSTD ZLIB BZip2
            LIBXML2 BSDXML EXPAT WIN32_XMLLITE PCREPOSIX PCRE2POSIX LIBGCC CNG
            XATTR ACL ICONV)
  set(ENABLE_${dep} OFF)
endforeach()
set(POSIX_REGEX_LIB NONE CACHE STRING "" FORCE)
add_subdirectory(${SUBMODULES}/libarchive ${CMAKE_CURRENT_BINARY_DIR}/libarchive
  EXCLUDE_FROM_ALL)

# We only read archives and extract them to disk, so leave out reading from
# disk and every archive writer. Those are also the parts that need POSIX
# directory and timezone APIs classic Mac OS doesn't have.
get_target_property(archive_sources archive_static SOURCES)
list(FILTER archive_sources EXCLUDE REGEX
  "archive_(read_disk|write_set_format|write_add_filter)[^/]*\\.c$")
set_property(TARGET archive_static PROPERTY SOURCES ${archive_sources})

if(CMAKE_SYSTEM_NAME MATCHES Retro)
  # xxhash.c types U32 as uint32_t under C99, but its header declares the
  # same functions with unsigned int. Retro68's uint32_t is unsigned long, so
  # build it as C89, where it uses unsigned int too.
  set_source_files_properties(${SUBMODULES}/libarchive/libarchive/xxhash.c
    TARGET_DIRECTORY archive_static PROPERTIES COMPILE_OPTIONS -std=gnu89)
endif()

target_compile_definitions(archive_static PRIVATE
  HAVE_LIBZ=1 HAVE_ZLIB_H=1
  HAVE_LIBBZ2=1 HAVE_BZLIB_H=1
  HAVE_LIBLZMA=1 HAVE_LZMA_H=1
  HAVE_LIBZSTD=1 HAVE_ZSTD_H=1 HAVE_ZSTD_compressStream=1
  HAVE_LIBLZ4=1 HAVE_LZ4_H=1 HAVE_LZ4HC_H=1
  HAVE_LIBMBEDCRYPTO=1 HAVE_MBEDTLS_AES_H=1 HAVE_MBEDTLS_MD_H=1
  HAVE_MBEDTLS_PKCS5_H=1 HAVE_MBEDTLS_VERSION_H=1
  ARCHIVE_CRYPTO_MD5_MBEDTLS=1 ARCHIVE_CRYPTO_RMD160_MBEDTLS=1
  ARCHIVE_CRYPTO_SHA1_MBEDTLS=1 ARCHIVE_CRYPTO_SHA256_MBEDTLS=1
  ARCHIVE_CRYPTO_SHA384_MBEDTLS=1 ARCHIVE_CRYPTO_SHA512_MBEDTLS=1)
# libarchive uses the plain target_link_libraries signature on this target,
# so this has to as well.
target_link_libraries(archive_static
  zlib bz2 liblzma zstd lz4 mbedcrypto)
