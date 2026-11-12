#pragma once

// A minimal check for the chat stress tests: a failed condition throws, the
// test's main reports it and exits non-zero.

#include <stdexcept>
#include <string>

namespace chat_stress_tests {

inline void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

inline void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace chat_stress_tests
