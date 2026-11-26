#ifndef IR_CORPUS_SOURCES_H
#define IR_CORPUS_SOURCES_H
#include "server_parts.h"
namespace web {
bool corpus_sources_route(const char *path, Text &body, const char *&type, int &status);
int corpus_sources_tests();
}
#endif
