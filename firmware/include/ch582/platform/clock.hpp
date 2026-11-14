#pragma once

#include <cstdint>

namespace ch582::clock
{

// HSE 32 MHz -> PLL 480 MHz -> /6 = 80 MHz system clock.
void                           init() noexcept;

inline constexpr std::uint32_t kSysClockHz = 60'000'000u;

// SysTick runs from HCLK/8.
inline constexpr std::uint32_t kSysTickHz   = kSysClockHz / 8u;
inline constexpr std::uint32_t kTicksPerMs  = kSysTickHz / 1000u;
inline constexpr std::uint32_t kTicksPerUs  = 7u;

inline constexpr std::uint8_t  kPll = 8;               // 480 MHz / 8 = 60 MHz

} // namespace ch582::clock
