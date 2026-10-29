/* Minimal mbed TLS configuration: only what libarchive uses, which is AES and
 * PBKDF2/HMAC for WinZip AES encrypted zips, and its digest functions. Leaves out entropy, networking and timing, which aren't available
 * everywhere we build (classic Mac OS). VERSION_C is for
 * archive_version_details(). */

#define MBEDTLS_AES_C
#define MBEDTLS_MD_C
#define MBEDTLS_MD5_C
#define MBEDTLS_RIPEMD160_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_PKCS5_C
#define MBEDTLS_VERSION_C
