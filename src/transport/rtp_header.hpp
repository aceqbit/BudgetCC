#pragma once
// RFC 3550 fixed 12-byte RTP header, network byte order. The 16-bit sequence number is unwrapped at the receiver.
#include <array>
#include <cstddef>
#include <cstdint>

namespace bcc {

constexpr std::size_t kRtpHeaderSize = 12;

struct RtpHeader {
  bool marker = false;
  std::uint8_t payload_type = 96;
  std::uint16_t seq = 0;
  std::uint32_t timestamp = 0;
  std::uint32_t ssrc = 0;
  bool operator==(const RtpHeader& o) const {
    return marker == o.marker && payload_type == o.payload_type && seq == o.seq && timestamp == o.timestamp &&
           ssrc == o.ssrc;
  }
};

void encodeRtp(const RtpHeader& h, std::uint8_t* out);
// False if the buffer is shorter than 12 bytes or the version is not 2.
bool decodeRtp(const std::uint8_t* buf, std::size_t len, RtpHeader& out);

// Extends 16-bit sequence numbers to 64 bits, tolerating small reordering.
class SeqUnwrapper {
 public:
  std::uint64_t unwrap(std::uint16_t seq);

 private:
  bool have_ = false;
  std::uint64_t last_ = 0;
};

}  // namespace bcc
