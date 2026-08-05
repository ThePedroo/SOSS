/* INFO: Minimal hand-declared Expat (XML parser) API used by libfloatingfeature. */

#ifndef FLOATING_FEATURE_EXPAT_H
#define FLOATING_FEATURE_EXPAT_H

typedef char XML_Char;

typedef struct XML_ParserStruct *XML_Parser;

typedef void (*XML_StartElementHandler)(void *user_data, const XML_Char *name, const XML_Char **atts);

typedef void (*XML_EndElementHandler)(void *user_data, const XML_Char *name);

typedef void (*XML_CharacterDataHandler)(void *user_data, const XML_Char *s, int len);

XML_Parser XML_ParserCreate(const XML_Char *encoding);

void XML_SetUserData(XML_Parser parser, void *user_data);

void XML_SetElementHandler(XML_Parser parser, XML_StartElementHandler start, XML_EndElementHandler end);

void XML_SetCharacterDataHandler(XML_Parser parser, XML_CharacterDataHandler handler);

int XML_Parse(XML_Parser parser, const char *s, int len, int is_final);

void XML_ParserFree(XML_Parser parser);

#endif /* FLOATING_FEATURE_EXPAT_H */
