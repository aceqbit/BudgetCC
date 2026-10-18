#pragma once
#include <iosfwd>
#include <string>
#include <vector>

namespace bcc {

// Entry point of the `budgetcc` executable. args[0] is the program name.
// Returns 0 on success, 2 for usage/validation errors, 1 for runtime failures.
int runMain(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace bcc
