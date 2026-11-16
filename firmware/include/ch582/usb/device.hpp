#pragma once

#include <cstddef>
#include <cstdint>

#include "ch582/compiler.hpp"
#include "ch582/platform/registers.hpp"
#include "ch582/serial/line_coding.hpp"
#include "ch582/util/ring_buffer.hpp"

namespace ch582::usb
{

// CDC ACM device on the Type-C USB controller (USB, 0x40008000).
class Device
{
  public:
    static Device&       instance();

    void                 init();
    void                 poll();
    CH582_HIGH_CODE void irq_handler();

    // Bytes to send to the PC. The application owns this ring.
    void                 set_tx_buffer(RingBuffer<512>* tx) noexcept
    {
        m_tx = tx;
    }

    std::size_t read_host(void* dst, std::size_t max_length) noexcept;

    bool        enumerated() const noexcept
    {
        return (usbreg::dev_addr() & 0x7Fu) != 0;
    }

    const serial::LineCoding& line_coding() const noexcept
    {
        return m_line_coding;
    }

    std::uint32_t line_coding_generation() const noexcept
    {
        return m_line_coding_generation;
    }

    bool consume_break() noexcept
    {
        CriticalSection guard;
        const bool      pending = m_break_pending;
        m_break_pending         = false;
        return pending;
    }

    std::uint8_t control_state() const noexcept
    {
        return m_control_state;
    }

  private:
    Device()                                    = default;

    static constexpr std::size_t kEp0Size       = 64;
    static constexpr std::size_t kPacketSize    = 64;
    static constexpr std::size_t kUsbBufferSize = 512;

    struct DeviceInfo
    {
        std::uint8_t config  = 0;
        std::uint8_t address = 0;
    };

    struct SetupRequest
    {
        std::uint8_t request_type;
        std::uint8_t request;
        std::uint8_t value_l;
        std::uint8_t value_h;
        std::uint8_t index_l;
        std::uint8_t index_h;
        std::uint8_t length_l;
        std::uint8_t length_h;
    };

    static_assert(sizeof(SetupRequest) == 8, "USB setup packet is eight bytes");

    void                   endpoint_init();
    CH582_HIGH_CODE void   handle_setup();
    void                   handle_ep0_in();

    DeviceInfo             m_device_info;
    std::uint8_t           m_setup_request = 0;
    std::uint8_t           m_setup_type    = 0;
    std::uint8_t           m_setup_length  = 0;
    const std::uint8_t*    m_descriptor    = nullptr;

    serial::LineCoding     m_line_coding{115200u, serial::kStopBits1, serial::kParityNone, 8};
    volatile std::uint32_t m_line_coding_generation = 0;
    volatile std::uint8_t  m_control_state          = 0;
    volatile bool          m_break_pending          = false;

    alignas(4) std::uint8_t m_ep0_buffer[kEp0Size]{};
    alignas(4) std::uint8_t m_ep1_buffer[16]{};
    alignas(4) std::uint8_t m_ep2_buffer[2 * kPacketSize]{};

    RingBuffer<kUsbBufferSize> m_rx_buffer;
    RingBuffer<512>*           m_tx             = nullptr;
    bool                       m_tx_zlp_pending = false;
};

} // namespace ch582::usb
