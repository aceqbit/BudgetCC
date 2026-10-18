#include <iostream>
#include <string>
#include <vector>

#include "harness/cli.hpp"

int main(int argc, char** argv) {
  std::vector<std::string> args(argv, argv + argc);
  return bcc::runMain(args, std::cout, std::cerr);
}
