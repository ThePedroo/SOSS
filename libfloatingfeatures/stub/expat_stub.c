#include <stddef.h>

#include "expat.h"

XML_Parser XML_ParserCreate(const XML_Char *encoding) {
  (void) encoding;

  return NULL;
}

void XML_SetUserData(XML_Parser parser, void *user_data) {
  (void) parser; (void) user_data;
}

void XML_SetElementHandler(XML_Parser parser, XML_StartElementHandler start, XML_EndElementHandler end) {
  (void) parser; (void) start; (void) end;
}

void XML_SetCharacterDataHandler(XML_Parser parser, XML_CharacterDataHandler handler) {
  (void) parser; (void) handler;
}

int XML_Parse(XML_Parser parser, const char *s, int len, int is_final) {
  (void) parser; (void) s; (void) len; (void) is_final;

  return 0;
}

void XML_ParserFree(XML_Parser parser) {
  (void) parser;
}
