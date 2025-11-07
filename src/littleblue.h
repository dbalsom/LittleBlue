#pragma once

#include <string>
#include <format>

inline std::string decimal(int n, int w = 0) {
    return std::format("{:0{}}", n, w);
}
