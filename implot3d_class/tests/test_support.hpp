#pragma once

// A minimal check for the implot3d_class tests: a failed condition throws, the
// test's main reports it and exits non-zero.

#include <stdexcept>
#include <string>

namespace implot3d_class_tests {

inline void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

inline void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace implot3d_class_tests
