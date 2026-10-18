#include "transport/rtp_header.hpp"

namespace bcc {

void encodeRtp(const RtpHeader& h, std::uint8_t* out) {
  out[0] = 0x80;  // V=2, P=0, X=0, CC=0
  out[1] = static_cast<std::uint8_t>((h.marker ? 0x80U : 0U) | (h.payload_type & 0x7fU));
  out[2] = static_cast<std::uint8_t>(h.seq >> 8);
  out[3] = static_cast<std::uint8_t>(h.seq);
  for (int i = 0; i < 4; ++i) {
    out[4 + i] = static_cast<std::uint8_t>(h.timestamp >> (24 - 8 * i));
    out[8 + i] = static_cast<std::uint8_t>(h.ssrc >> (24 - 8 * i));
  }
}

bool decodeRtp(const std::uint8_t* buf, std::size_t len, RtpHeader& out) {
  if (len < kRtpHeaderSize) return false;
  if ((buf[0] >> 6) != 2) return false;
  out.marker = (buf[1] & 0x80U) != 0;
  out.payload_type = buf[1] & 0x7fU;
  out.seq = static_cast<std::uint16_t>((buf[2] << 8) | buf[3]);
  out.timestamp = 0;
  out.ssrc = 0;
  for (int i = 0; i < 4; ++i) {
    out.timestamp = (out.timestamp << 8) | buf[4 + i];
    out.ssrc = (out.ssrc << 8) | buf[8 + i];
  }
  return true;
}

std::uint64_t SeqUnwrapper::unwrap(std::uint16_t seq) {
  if (!have_) {
    have_ = true;
    last_ = seq;
    return last_;
  }
  const std::uint16_t last16 = static_cast<std::uint16_t>(last_);
  const std::int16_t diff = static_cast<std::int16_t>(static_cast<std::uint16_t>(seq - last16));
  const std::uint64_t result = last_ + static_cast<std::uint64_t>(static_cast<std::int64_t>(diff));
  if (diff > 0) last_ = result;
  return result;
}

}  // namespace bcc
