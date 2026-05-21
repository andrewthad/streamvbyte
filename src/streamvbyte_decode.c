#include "streamvbyte.h"

#include <string.h> // for memcpy
#include <stdio.h> // for printf, get rid of this
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

/* GCC-compatible compiler, targeting x86/x86-64 */
#include <x86intrin.h>

// Note: Make sure that the Makefile has SSE4.1 and AVX2 turned on when building
// this file.

static inline void streamvbyte_build_shuffle(__m128i* restrict out_shuf, uint32_t* restrict out_len, uint32_t key) {
      const __m128i BASE_PATTERN = _mm_setr_epi8(0,1,2,3, 0,1,2,3, 0,1,2,3, 0,1,2,3);
      const __m128i LANE_BCAST  = _mm_setr_epi8(0,0,0,0, 1,1,1,1, 2,2,2,2, 3,3,3,3);

      // BMI2 PDEP: spread 2-bit codes into individual bytes
      uint32_t codes_bytes = _pdep_u32(key, 0x03030303);
      uint32_t widths_bytes = codes_bytes + 0x01010101;  // width = code + 1

      // Exclusive prefix sum via inclusive prefix sum (multiply) shifted left one byte
      uint32_t offsets_bytes_inclusive = widths_bytes * 0x01010101;
      uint32_t offsets_bytes_exclusive = offsets_bytes_inclusive << 8;

      // The high 8-bit element in offsets_bytes_inclusive is all of the  
      // lengths added together.
      uint32_t len = offsets_bytes_inclusive >> 24;

      // Broadcast each byte to its 4-byte lane (single shuffle, no PACK_LOW needed)
      __m128i off = _mm_shuffle_epi8(_mm_cvtsi32_si128(offsets_bytes_exclusive), LANE_BCAST);
      __m128i codes_bcast = _mm_shuffle_epi8(_mm_cvtsi32_si128(codes_bytes), LANE_BCAST);

      // Build shuffle mask: invalid positions set to 0xFF (bit 7 set zeroes shuffle output)
      __m128i shuf = _mm_add_epi8(BASE_PATTERN, off);
      __m128i invalid = _mm_cmpgt_epi8(BASE_PATTERN, codes_bcast); // cmpgt returns 0x00 or 0xFF
      shuf = _mm_or_si128(shuf, invalid);
      *out_shuf = shuf;
      *out_len = len;
}

static inline __m128i streamvbyte_decode_4x32_apply(__m128i shuf, const uint8_t* restrict dataPtr) {
      __m128i raw_data = _mm_loadu_si128((const __m128i*)dataPtr);
      __m128i reconstruction = _mm_shuffle_epi8(raw_data, shuf);
      return reconstruction;
}

// Behavior:
// * This processes 4 elements from the data stream.
// * Only the low 8 bits of key are used. These are interpreted as
//   four 2-bit unsigned integers.
static inline __m128i streamvbyte_decode_4x32(uint32_t* restrict out, uint32_t key, const uint8_t* restrict dataPtr) {
      const __m128i BASE_PATTERN = _mm_setr_epi8(0,1,2,3, 0,1,2,3, 0,1,2,3, 0,1,2,3);
      const __m128i LANE_BCAST  = _mm_setr_epi8(0,0,0,0, 1,1,1,1, 2,2,2,2, 3,3,3,3);

      // BMI2 PDEP: spread 2-bit codes into individual bytes
      uint32_t codes_bytes = _pdep_u32(key, 0x03030303);
      uint32_t widths_bytes = codes_bytes + 0x01010101;  // width = code + 1

      // Exclusive prefix sum via inclusive prefix sum (multiply) shifted left one byte
      uint32_t offsets_bytes_inclusive = widths_bytes * 0x01010101;
      uint32_t offsets_bytes_exclusive = offsets_bytes_inclusive << 8;

      // The high 8-bit element in offsets_bytes_inclusive is all of the  
      // lengths added together.
      uint32_t len = offsets_bytes_inclusive >> 24;

      // After this point, we only need: offsets_bytes_exclusive, codes_bytes

      __m128i raw_data = _mm_loadu_si128((const __m128i*)dataPtr);

      // Broadcast each byte to its 4-byte lane (single shuffle, no PACK_LOW needed)
      __m128i off = _mm_shuffle_epi8(_mm_cvtsi32_si128(offsets_bytes_exclusive), LANE_BCAST);
      __m128i codes_bcast = _mm_shuffle_epi8(_mm_cvtsi32_si128(codes_bytes), LANE_BCAST);

      // Build shuffle mask: invalid positions set to 0xFF (bit 7 set zeroes shuffle output)
      __m128i shuf = _mm_add_epi8(BASE_PATTERN, off);
      __m128i invalid = _mm_cmpgt_epi8(BASE_PATTERN, codes_bcast); // cmpgt returns 0x00 or 0xFF
      shuf = _mm_or_si128(shuf, invalid);

      __m128i reconstruction = _mm_shuffle_epi8(raw_data, shuf);
      *out = len;
      return reconstruction;
}

const uint8_t* streamvbyte_decode_32(uint32_t *out, const uint8_t* restrict keyPtr, const uint8_t* restrict dataPtr) {
    const uint64_t *keyPtr64 = (const uint64_t *)keyPtr;
    uint64_t keys;
    memcpy(&keys, keyPtr64, 8);
    for(uint32_t j = 0; j < 8; j = j + 1) {
      uint32_t len;
      __m128i reconstruction = streamvbyte_decode_4x32(&len, ((keys >> (8 * j)) & 0xFF), dataPtr);
      _mm_storeu_si128((__m128i *)(out + (j * 4)), reconstruction);
      dataPtr = dataPtr + len;
    }
    return dataPtr;
}

// This does not perform as well. I hoped that breaking the dependency on length
// would improve things, but it does not.
const uint8_t* streamvbyte_decode_32_experimental(uint32_t *out, const uint8_t* restrict keyPtr, const uint8_t* restrict dataPtr) {
    const uint64_t *keyPtr64 = (const uint64_t *)keyPtr;
    uint64_t keys;
    memcpy(&keys, keyPtr64, 8);
    __m128i shuffles[8];
    uint32_t lens[8];
    for(uint32_t j = 0; j < 8; j = j + 1) {
      streamvbyte_build_shuffle(shuffles + j, lens + j, (keys >> (8 * j)) & 0xFF);
    }
    for(uint32_t j = 0; j < 8; j = j + 1) {
      __m128i reconstruction = streamvbyte_decode_4x32_apply(shuffles[j], dataPtr);
      _mm_storeu_si128((__m128i *)(out + (j * 4)), reconstruction);
      dataPtr = dataPtr + lens[j];
    }
    return dataPtr;
}

static inline const uint8_t *streamvbyte_decode_inner(uint32_t *out, const uint8_t* restrict keyPtr, const uint8_t* restrict dataPtr, uint64_t count) {
  uint64_t keybytes = count / 4; // number of key bytes
  uint64_t key_qwords = keybytes / 8; // number of key bytes
  for (uint64_t i = 0; i < key_qwords; i++) {
    dataPtr = streamvbyte_decode_32(out, keyPtr + (i * 8), dataPtr);
    out += 32;
  }
  return dataPtr;
}

// Read count 32-bit integers in maskedvbyte format from in, storing the result
// in out.  Returns the number of bytes read.
// Precondition: The count must be a multiple of 32.
size_t streamvbyte_decode(const uint8_t *in, uint32_t *out, uint32_t count) {
  if (count == 0)
    return 0;

  const uint8_t *keyPtr = in;               // full list of keys is next
  uint32_t keyLen = count / 4;              // 2-bits per key
  const uint8_t *dataPtr = keyPtr + keyLen; // data starts at end of keys

  dataPtr = streamvbyte_decode_inner(out, keyPtr, dataPtr, count);
  out += count;
  keyPtr += count/4;

  return (size_t)(dataPtr - in);
}

bool streamvbyte_validate_stream(const uint8_t *in, size_t inCount, uint32_t outCount) {
  if (outCount % 32 != 0)
    return false;
  if (inCount == 0 || outCount == 0)
    return inCount == outCount;

  // 2-bits per key (rounded up)
  // Note that we don't add to outCount in case it overflows
  uint32_t keyLen = outCount / 4;
  if (outCount & 3)
    keyLen++;

  // Check that there's enough space for the keys
  if (keyLen > inCount)
    return false;

  // Accumulate the key sizes in a wider type to avoid overflow
  const uint8_t *keyPtr = in;
  uint64_t encodedSize = 0;

  // Give the compiler a hint that it can avoid branches in the inner loop
  for (uint32_t c = 0; c < outCount / 4; c++) {
    uint32_t key = *keyPtr++;
    for (uint8_t shift = 0; shift < 8; shift += 2) {
      const uint8_t code = (key >> shift) & 0x3;
      encodedSize += code + 1;
    }
  }
  outCount &= 3;

  // Process the remainder one at a time
  uint8_t shift = 0;
  uint32_t key = *keyPtr++;
  for (uint32_t c = 0; c < outCount; c++) {
    if (shift == 8) {
      shift = 0;
      key = *keyPtr++;
    }
    const uint8_t code = (key >> shift) & 0x3;
    encodedSize += code + 1;
    shift += 2;
  }

  return encodedSize == inCount - keyLen;
}
