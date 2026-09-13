/*
 * Link stubs for uzlib's checksum helpers.
 *
 * The vendored lib/uzlib ships only tinflate.c; upstream's adler32.c / crc32.c
 * are absent because the firmware never calls uzlib_uncompress_chksum() (the
 * only referrer) and the embedded linker garbage-collects it. The host linker
 * links the whole tinflate.o, so the two symbols must exist. They are
 * unreachable from InflateReader::read() -> uzlib_uncompress(), which is the
 * only entry point the dictionary code uses; abort() makes any future
 * accidental use loud instead of silently wrong.
 */

#include <stdint.h>
#include <stdlib.h>

uint32_t uzlib_adler32(const void* data, unsigned int length, uint32_t prev_sum) {
  (void)data;
  (void)length;
  (void)prev_sum;
  abort();
}

uint32_t uzlib_crc32(const void* data, unsigned int length, uint32_t crc) {
  (void)data;
  (void)length;
  (void)crc;
  abort();
}
