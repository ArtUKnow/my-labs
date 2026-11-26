#ifndef IR_INSPECTION_H
#define IR_INSPECTION_H
#include "server_parts.h"
namespace web {
bool inspection_route(const char *path, const char *parameters, Text &body, const char *&type, int &status);
int inspection_tests();
int ir_inspection_main(int argc, char **argv);
}
#endif
