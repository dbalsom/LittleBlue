#ifndef MAIN_H
#define MAIN_H

#include <string>
#include <__format/format_functions.h>

inline std::string decimal(int n, int w = 0) {
    return std::format("{:0{}}", n, w);
}

#endif //MAIN_H
