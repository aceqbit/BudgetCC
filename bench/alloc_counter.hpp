#pragma once
#include <cstdint>

namespace bcc {

// Global operator new/delete are replaced in alloc_counter.cpp; this is the number of operator new calls so far.
std::uint64_t allocationCount();
// False in sanitizer builds: ASan and TSan own operator new there, so the counter stays at zero.
bool allocationCounterActive();

}  // namespace bcc
