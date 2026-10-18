#pragma once
#include <cstddef>

namespace bcc {

// Destructive-interference size used to keep producer and consumer state on separate cache lines.
constexpr std::size_t kCacheLine = 64;

}  // namespace bcc
