#ifndef IR_INSPECTION_H
#define IR_INSPECTION_H
#include "server_parts.h"
namespace web {
bool inspection_route(const char *path, const char *parameters, Text &body, const char *&type, int &status);
bool inspection_command(const char *kind, Text &command);
int inspection_execute(const char *kind, Text &out);
int inspection_tests();
int ir_inspection_main(int argc, char **argv);
}
#endif
