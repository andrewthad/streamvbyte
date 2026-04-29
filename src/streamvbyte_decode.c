#include "streamvbyte.h"
#include "streamvbyte_isadetection.h"
#include "streamvbyte_shuffle_tables_decode.h"

#include <string.h> // for memcpy
#include <stdio.h> // for printf, get rid of this

#ifdef __clang__
#pragma clang diagnostic ignored "-Wdeclaration-after-statement"
#pragma clang diagnostic ignored "-Wcast-align"
#endif

STREAMVBYTE_TARGET_SSE41
static inline __m128i svb_decode_sse41(uint32_t key,
                                  const uint8_t *__restrict__ *dataPtrPtr) {
  uint8_t len;
  __m128i Data = _mm_loadu_si128((const __m128i *)*dataPtrPtr);
  uint8_t *pshuf = (uint8_t *) &shuffleTable[key];
  __m128i Shuf = *(__m128i *)pshuf;
  len = pshuf[12 + (key >> 6)] + 1;
  Data = _mm_shuffle_epi8(Data, Shuf);
  *dataPtrPtr += len;
  return Data;
}
STREAMVBYTE_UNTARGET_REGION


STREAMVBYTE_TARGET_SSE41
static inline void svb_write_sse41(uint32_t *out, __m128i Vec) {
  _mm_storeu_si128((__m128i *)out, Vec);
}
STREAMVBYTE_UNTARGET_REGION



STREAMVBYTE_TARGET_SSE41
static inline const uint8_t *svb_decode_sse41_simple(uint32_t *out,
                                     const uint8_t *__restrict__ keyPtr,
                                     const uint8_t *__restrict__ dataPtr,
                                     uint64_t count) {

  uint64_t keybytes = count / 4; // number of key bytes
  __m128i Data;

  // Drew: There was originally a check here to make sure that [keybytes >=8],
  // but I've removed it when I eliminated support for compression of arrays
  // whose length is not a multiple of 32.
  int64_t Offset = -(int64_t)keybytes / 8 + 1;

  const uint64_t *keyPtr64 = (const uint64_t *)keyPtr - Offset;
  uint64_t nextkeys;
  memcpy(&nextkeys, keyPtr64 + Offset, sizeof(nextkeys));
  for (; Offset != 1; ++Offset) {
    uint64_t keys = nextkeys;
    memcpy(&nextkeys, keyPtr64 + Offset + 1, sizeof(nextkeys));

    Data = svb_decode_sse41((keys & 0xFF), &dataPtr);
    svb_write_sse41(out, Data);
    Data = svb_decode_sse41((keys & 0xFF00) >> 8, &dataPtr);
    svb_write_sse41(out + 4, Data);

    keys >>= 16;
    Data = svb_decode_sse41((keys & 0xFF), &dataPtr);
    svb_write_sse41(out + 8, Data);
    Data = svb_decode_sse41((keys & 0xFF00) >> 8, &dataPtr);
    svb_write_sse41(out + 12, Data);

    keys >>= 16;
    Data = svb_decode_sse41((keys & 0xFF), &dataPtr);
    svb_write_sse41(out + 16, Data);
    Data = svb_decode_sse41((keys & 0xFF00) >> 8, &dataPtr);
    svb_write_sse41(out + 20, Data);

    keys >>= 16;
    Data = svb_decode_sse41((keys & 0xFF), &dataPtr);
    svb_write_sse41(out + 24, Data);
    Data = svb_decode_sse41((keys & 0xFF00) >> 8, &dataPtr);
    svb_write_sse41(out + 28, Data);

    out += 32;
  }
  return dataPtr;
}
STREAMVBYTE_UNTARGET_REGION

// Read count 32-bit integers in maskedvbyte format from in, storing the result
// in out.  Returns the number of bytes read.
size_t streamvbyte_decode(const uint8_t *in, uint32_t *out, uint32_t count) {
  if (count == 0)
    return 0;
  if ((count % 32) != 0) {
    fprintf(stderr, "Bad count: %llu\n", (long long unsigned)count);
    exit(1);
  }

  const uint8_t *keyPtr = in;               // full list of keys is next
  uint32_t keyLen = ((count + 3) / 4);      // 2-bits per key (rounded up)
  const uint8_t *dataPtr = keyPtr + keyLen; // data starts at end of keys

  dataPtr = svb_decode_sse41_simple(out, keyPtr, dataPtr, count);
  out += count & ~ 31U;
  keyPtr += (count/4) & ~ 7U;
  count &= 31;

  return (size_t)(dataPtr - in);
}

bool streamvbyte_validate_stream(const uint8_t *in, size_t inCount,
                                 uint32_t outCount) {
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
