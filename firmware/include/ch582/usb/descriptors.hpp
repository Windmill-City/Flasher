#pragma once

#include <cstddef>
#include <cstdint>

namespace ch582::usb
{

inline constexpr std::size_t   kCdcLineCodingSize  = 7;

// Flasher CDC ACM device. The Dongle itself is 1A86:5720, so this is a
// distinct product ID to keep the two COM ports apart.
inline constexpr std::uint16_t kVendorId           = 0x1A86;
inline constexpr std::uint16_t kProductId          = 0x5721;

inline constexpr std::uint8_t  kCdcInterface       = 0;
inline constexpr std::uint8_t  kCdcDataInterface   = 1;

inline constexpr std::uint8_t  kDeviceDescriptor[] = {
    0x12,
    0x01,
    0x00,
    0x02,
    0xEF,
    0x02,
    0x01,
    0x40,
    0x86,
    0x1A,
    0x21,
    0x57,
    0x00,
    0x01,
    0x00,
    0x00,
    0x00,
    0x01,
};

// One CDC ACM function: notification endpoint 0x81 and bulk pair 0x02/0x82.
inline constexpr std::uint8_t kConfigDescriptor[] = {
    0x09, 0x02, 0x4B, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,

    0x08, 0x0B, 0x00, 0x02, 0x02, 0x02, 0x01, 0x00,

    0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00, 0x05, 0x24, 0x00, 0x10, 0x01, 0x05, 0x24, 0x01, 0x00,
    0x01, 0x04, 0x24, 0x02, 0x02, 0x05, 0x24, 0x06, 0x00, 0x01, 0x07, 0x05, 0x81, 0x03, 0x10, 0x00, 0x10,

    0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00, 0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00, 0x07, 0x05,
    0x82, 0x02, 0x40, 0x00, 0x00,
};
static_assert(sizeof(kConfigDescriptor) == 0x4B, "configuration length");

struct DescriptorSet
{
    const std::uint8_t* device;
    const std::uint8_t* config;
};

inline constexpr std::uint8_t kDescriptorDevice = 1;
inline constexpr std::uint8_t kDescriptorConfig = 2;

inline const std::uint8_t* find_descriptor(const DescriptorSet& set, std::uint8_t type) noexcept
{
    switch (type)
    {
    case kDescriptorDevice:
        return set.device;
    case kDescriptorConfig:
        return set.config;
    default:
        return nullptr;
    }
}

inline constexpr DescriptorSet kDescriptors = {
    kDeviceDescriptor,
    kConfigDescriptor,
};

} // namespace ch582::usb
