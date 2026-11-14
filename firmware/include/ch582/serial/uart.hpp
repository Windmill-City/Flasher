#pragma once

#include <cstddef>
#include <cstdint>

#include "ch582/compiler.hpp"
#include "ch582/serial/line_coding.hpp"
#include "ch582/util/ring_buffer.hpp"

namespace ch582::serial
{

// UART1 bridged onto the USB-A connector:
//   PB13 = TXD1_ (D+ / Dongle PA1 RXD)
//   PB12 = RXD1_ (D- / Dongle PA0 TXD)
class Uart
{
  public:
    static constexpr std::uint32_t kBaudRate     = 115200;
    static constexpr std::size_t   kRxBufferSize = 512;

    void        init(std::uint32_t baud = kBaudRate) noexcept;
    void        deinit() noexcept;
    void        configure(const LineCoding& coding) noexcept;

    CH582_HIGH_CODE std::size_t read(std::uint8_t* dst, std::size_t max_length) noexcept;
    void                        write(const std::uint8_t* src, std::size_t length) noexcept;
    CH582_HIGH_CODE void        irq_handler() noexcept;

    bool                        active() const noexcept
    {
        return m_active;
    }

  private:
    void        set_baud(std::uint32_t baud) noexcept;

    RingBuffer<kRxBufferSize> m_rx;
    bool                      m_active = false;
};

} // namespace ch582::serial
