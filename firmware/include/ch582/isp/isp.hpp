#pragma once

#include <cstddef>
#include <cstdint>

#include "ch582/usb/host.hpp"

namespace ch582::isp
{

// WCH USB ISP host used against the Dongle bootloader (4348/1A86:55E0).
class Isp
{
  public:
    struct Info
    {
        std::uint8_t  chip_id     = 0;
        std::uint8_t  device_type = 0;
        std::uint8_t  uid[8]{};
        std::uint32_t erase_sectors = 0;
    };

    explicit Isp(usbhost::Host& host) noexcept
        : m_host(host)
    {
    }

    bool begin(std::uint32_t image_size, Info& info) noexcept;
    bool program(std::uint32_t address, const std::uint8_t* data, std::uint8_t length) noexcept;
    bool verify(std::uint32_t address, const std::uint8_t* data, std::uint8_t length) noexcept;
    bool finish() noexcept;

    bool active() const noexcept
    {
        return m_active;
    }

    void abort() noexcept
    {
        m_active       = false;
        m_write_closed = false;
    }

    std::uint32_t next_address() const noexcept
    {
        return m_next_address;
    }

  private:
    bool transfer(const std::uint8_t* command,
                  std::uint8_t        length,
                  std::uint8_t*       response,
                  std::uint8_t&       response_length,
                  std::uint32_t       timeout_ms) noexcept;
    bool read_response(std::uint8_t* response, std::uint8_t& response_length, std::uint32_t timeout_ms) noexcept;
    bool send_key(std::uint8_t* response, std::uint8_t& response_length) noexcept;
    bool close_write_session(bool rekey) noexcept;
    void update_key() noexcept;

    usbhost::Host& m_host;
    Info           m_info;
    std::uint8_t   m_key[8]{};
    std::uint32_t  m_next_address = 0;
    std::uint8_t   m_padding      = 0xA5;
    bool           m_active       = false;
    bool           m_write_closed = false;
};

} // namespace ch582::isp
