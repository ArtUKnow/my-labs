#ifndef IR_DASHBOARD_H
#define IR_DASHBOARD_H
#include "server_parts.h"
namespace web {
bool dashboard_route(const char *path, const char *parameters, Text &body, const char *&content_type, int &status);
int dashboard_tests();
}
#endif
