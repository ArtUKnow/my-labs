#ifndef IR_EXAMPLES_H
#define IR_EXAMPLES_H
#include "server_parts.h"
namespace web {
bool examples_route(const char *path, const char *parameters, Text &body, const char *&type, int &status);
int examples_tests();
}
#endif
