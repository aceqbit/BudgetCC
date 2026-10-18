#pragma once
#include <fstream>
#include <string>

#include "telemetry/sample.hpp"

namespace bcc {

std::string csvHeader();
// Appends one CSV row (with trailing newline). Full-precision, locale-independent numbers.
void appendCsvRow(std::string& out, const Sample& s);

// Streams samples to a file; throws std::runtime_error if the file cannot be opened.
class CsvWriter {
 public:
  explicit CsvWriter(const std::string& path);
  void write(const Sample& s);
  void close();

 private:
  std::ofstream f_;
  std::string buf_;
};

}  // namespace bcc
