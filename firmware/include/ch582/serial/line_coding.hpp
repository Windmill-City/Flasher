#pragma once

#include <cstdint>

#include "ch582/compiler.hpp"

namespace ch582::serial
{

inline constexpr std::uint8_t kStopBits1   = 0;
inline constexpr std::uint8_t kStopBits1_5 = 1;
inline constexpr std::uint8_t kStopBits2   = 2;

inline constexpr std::uint8_t kParityNone  = 0;
inline constexpr std::uint8_t kParityOdd   = 1;
inline constexpr std::uint8_t kParityEven  = 2;
inline constexpr std::uint8_t kParityMark  = 3;
inline constexpr std::uint8_t kParitySpace = 4;

// Byte-for-byte CDC ACM line coding layout.
struct CH582_PACKED LineCoding
{
    std::uint32_t baud_rate;
    std::uint8_t  stop_bits;
    std::uint8_t  parity;
    std::uint8_t  data_bits;
};

static_assert(sizeof(LineCoding) == 7, "line coding matches the CDC payload");

} // namespace ch582::serial
