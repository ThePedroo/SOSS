#ifndef SNF_TEST_UTIL_H
#define SNF_TEST_UTIL_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include "decode_tables.h"

static const char fixture_plain[] =
  "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
  "<FeatureSet>\n"
  "  <CscFeature_Plain>plain</CscFeature_Plain>\n"
  "  <FeatureSet region=\"US\">\n"
  "    <CscFeature_On>true</CscFeature_On>\n"
  "    <CscFeature_Upper>TRUE</CscFeature_Upper>\n"
  "    <CscFeature_Off>false</CscFeature_Off>\n"
  "    <CscFeature_Number>42</CscFeature_Number>\n"
  "    <CscFeature_Negative>-7</CscFeature_Negative>\n"
  "    <CscFeature_Junk>12abc</CscFeature_Junk>\n"
  "    <CscFeature_Text>  padded  </CscFeature_Text>\n"
  "    <CscFeature_Entities>a&amp;b&lt;c&gt;d&quot;e&apos;f</CscFeature_Entities>\n"
  "    <CscFeature_Dup>one</CscFeature_Dup>\n"
  "    <CscFeature_Dup>two</CscFeature_Dup>\n"
  "    <CscFeature_Country>Samsung</CscFeature_Country>\n"
  "    <CscFeature_WithAttr lang=\"en\">attrtext</CscFeature_WithAttr>\n"
  "    <Outer x=\"1\"><Inner>quirk</Inner></Outer>\n"
  "  </FeatureSet>\n"
  "  <CscFeature_AfterNested>after</CscFeature_AfterNested>\n"
  "</FeatureSet>\n";

static const char fixture_malformed[] =
  "<?xml version=\"1.0\"?>\n"
  "<FeatureSet>\n"
  "  <CscFeature_First>first</CscFeature_First>\n"
  "  <CscFeature_Broken>";

static const char fixture_utf8[] =
  "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
  "<FeatureSet>\n"
  "  <CscFeature_Unicode>h\u00e9llo w\u00f6rld \u65e5\u672c\u8a9e</CscFeature_Unicode>\n"
  "</FeatureSet>\n";

static void snf_test_build_sized(char *buffer, size_t cap, size_t target) {
  const char *prefix = "<?xml version=\"1.0\"?><FeatureSet><A>";
  const char *suffix = "</A></FeatureSet>";
  size_t prefix_len = strlen(prefix);
  size_t suffix_len = strlen(suffix);
  size_t text_len = target - prefix_len - suffix_len;

  (void) cap;

  memcpy(buffer, prefix, prefix_len);
  memset(buffer + prefix_len, 'x', text_len);
  memcpy(buffer + prefix_len + text_len, suffix, suffix_len + 1);
}

/* INFO: Deflate an XML document into a gzip stream (windowBits 31),
           the format the library expects before encode(). */
static void snf_test_gzip(const char *xml, size_t xml_len, char **out, size_t *out_len) {
  z_stream stream = { 0 };
  deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);

  uLong bound = deflateBound(&stream, (uLong)xml_len);
  char *buffer = malloc((size_t)bound);

  stream.next_in = (Bytef *)xml;
  stream.avail_in = (uInt)xml_len;
  stream.next_out = (Bytef *)buffer;
  stream.avail_out = (uInt)bound;

  deflate(&stream, Z_FINISH);
  deflateEnd(&stream);

  *out = buffer;
  *out_len = (size_t)stream.total_out;
}

/* INFO: Samsung's encode transform, the inverse of decode. */
static void snf_test_encode(char *buffer, size_t len) {
  for (size_t i = 0; i < len; i++) {
    uint8_t shift = snf_decode_shift[i & 0xFF] & 7;
    uint8_t byte = (uint8_t)buffer[i] ^ snf_decode_xor[i & 0xFF];

    buffer[i] = (char)((byte >> shift) | (byte << (8 - shift)));
  }
}

/* INFO: Build an encoded gzip fixture (the on-disk format of an OMC-encoded cscfeature file). */
static void snf_test_gzip_encode(const char *xml, size_t xml_len, char **out, size_t *out_len) {
  char *gzipped = NULL;
  size_t gzip_len = 0;

  snf_test_gzip(xml, xml_len, &gzipped, &gzip_len);
  snf_test_encode(gzipped, gzip_len);

  *out = gzipped;
  *out_len = gzip_len;
}

#endif /* SNF_TEST_UTIL_H */
