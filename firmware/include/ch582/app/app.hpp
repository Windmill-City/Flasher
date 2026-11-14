#pragma once

#include <cstddef>
#include <cstdint>

#include "ch582/isp/isp.hpp"
#include "ch582/serial/uart.hpp"
#include "ch582/usb/device.hpp"
#include "ch582/usb/host.hpp"
#include "ch582/util/ring_buffer.hpp"

namespace ch582::app
{

inline constexpr std::uint8_t kCmdPing       = 0x01;
inline constexpr std::uint8_t kCmdInfo       = 0x02;
inline constexpr std::uint8_t kCmdSetMode    = 0x03;
inline constexpr std::uint8_t kCmdPower      = 0x04;

inline constexpr std::uint8_t kCmdIspBegin   = 0x10;
inline constexpr std::uint8_t kCmdIspWrite   = 0x11;
inline constexpr std::uint8_t kCmdIspVerify  = 0x12;
inline constexpr std::uint8_t kCmdIspFinish  = 0x13;

// Raw ISP passthrough: the PC drives the WCH bootloader protocol directly.
inline constexpr std::uint8_t kCmdIspOpen    = 0x14;
inline constexpr std::uint8_t kCmdIspRaw     = 0x15;
inline constexpr std::uint8_t kCmdIspClose   = 0x16;

inline constexpr std::uint8_t kStatusOk      = 0x00;
inline constexpr std::uint8_t kStatusBadCmd  = 0x01;
inline constexpr std::uint8_t kStatusBadArg  = 0x02;
inline constexpr std::uint8_t kStatusNoTarget = 0x03;
inline constexpr std::uint8_t kStatusUsb     = 0x04;
inline constexpr std::uint8_t kStatusIsp     = 0x05;
inline constexpr std::uint8_t kStatusBusy    = 0x06;
inline constexpr std::uint8_t kStatusUnsupported = 0x07;

inline constexpr std::uint8_t kModeIdle      = 0;
inline constexpr std::uint8_t kModeMaster    = 1;
inline constexpr std::uint8_t kModeSlave     = 2;

inline constexpr std::uint8_t kTargetNone    = 0;
inline constexpr std::uint8_t kTargetIsp     = 1;
inline constexpr std::uint8_t kTargetCdc     = 2;
inline constexpr std::uint8_t kTargetBulk    = 3;
inline constexpr std::uint8_t kTargetUart    = 4;

class App
{
  public:
    App(usb::Device& usb, usbhost::Host& host, serial::Uart& uart, isp::Isp& isp) noexcept
        : m_usb(usb)
        , m_host(host)
        , m_uart(uart)
        , m_isp(isp)
    {
    }

    void init() noexcept;
    void poll() noexcept;

  private:
    void  process_input() noexcept;
    void  feed(std::uint8_t byte) noexcept;
    void  process_frame() noexcept;
    void  reset_parser() noexcept;

    void  send_response(std::uint8_t command, const void* payload, std::uint8_t length) noexcept;
    void  send_status(std::uint8_t command, std::uint8_t status) noexcept;

    std::uint8_t enter_master() noexcept;
    std::uint8_t enter_slave(std::uint32_t baud) noexcept;
    std::uint8_t enter_isp_host() noexcept;
    void         stop_bridge() noexcept;
    void         bridge_poll() noexcept;
    void         handle_coding_change() noexcept;
    serial::LineCoding pc_line_coding() const noexcept;
    std::uint8_t target_kind() const noexcept;

    usb::Device&   m_usb;
    usbhost::Host& m_host;
    serial::Uart&  m_uart;
    isp::Isp&      m_isp;

    RingBuffer<512> m_tx;

    std::uint8_t  m_mode = kModeIdle;

    std::uint8_t  m_frame[96]{};
    std::size_t   m_frame_length = 0;

    std::uint8_t  m_bulk_in  = 0;
    std::uint8_t  m_bulk_out = 0;
    std::uint8_t  m_cdc_itf  = 0;

    std::uint8_t  m_pending[64]{};
    std::uint8_t  m_pending_length = 0;

    std::uint32_t m_seen_coding_generation = 0;
    std::uint8_t  m_seen_control_state     = 0;
};

} // namespace ch582::app
