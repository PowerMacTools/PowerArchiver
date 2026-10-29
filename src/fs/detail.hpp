#pragma once

#include <string>

// Implemented separately for each platform, for fs.cpp.
namespace fs::detail {

// Turns one component of an archive member path into a valid file name.
std::string component(const std::string &name);

} // namespace fs::detail
