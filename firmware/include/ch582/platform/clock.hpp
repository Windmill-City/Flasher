#pragma once

#include <cstdint>

namespace ch582::clock
{

// HSE 32 MHz -> PLL 480 MHz -> /6 = 80 MHz system clock.
void                           init() noexcept;

inline constexpr std::uint32_t kSysClockHz = 80'000'000u;

// SysTick runs from HCLK/8.
inline constexpr std::uint32_t kSysTickHz  = kSysClockHz / 8u;
inline constexpr std::uint32_t kTicksPerMs = kSysTickHz / 1000u;
inline constexpr std::uint32_t kTicksPerUs = 10u;

inline constexpr std::uint8_t  kPll        = 6; // 480 MHz / 6 = 80 MHz

} // namespace ch582::clock
