#pragma once

#include <cstddef>
#include <cstdint>

#include "ch582/platform/registers.hpp"
#include "ch582/serial/line_coding.hpp"

namespace ch582::usbhost
{

struct DeviceInfo
{
    std::uint16_t vid          = 0;
    std::uint16_t pid          = 0;
    std::uint8_t  config_value = 1;
    std::uint8_t  bulk_in      = 0;
    std::uint8_t  bulk_out     = 0;
    std::uint8_t  cdc_comm_itf = 0;
    bool          has_cdc      = false;
};

enum class Result : std::uint8_t
{
    ok,
    nak,
    stall,
    timeout,
    disconnected,
    error,
};

// USB2 host controller on the USB-A connector.
class Host
{
  public:
    void   init() noexcept;
    void   disable() noexcept;
    void   power_off() noexcept;
    void   vbus(bool on) noexcept;
    void   power_cycle_isp() noexcept;
    void   power_cycle_master() noexcept;
    void   power_cycle_app() noexcept;
    void   power_cycle_slave() noexcept;

    bool   attached() const noexcept;
    Result enumerate(DeviceInfo& info, std::uint32_t timeout_ms = 800) noexcept;

    Result set_line_coding(std::uint8_t itf, const serial::LineCoding& coding) noexcept;
    Result set_control_line_state(std::uint8_t itf, std::uint16_t state) noexcept;

    Result bulk_out(std::uint8_t ep, const void* data, std::uint8_t length, std::uint32_t timeout_us = 2000) noexcept;
    Result bulk_in(std::uint8_t ep, void* data, std::uint8_t& length, std::uint32_t timeout_us = 2000) noexcept;

    const DeviceInfo& info() const noexcept
    {
        return m_info;
    }

    bool is_isp_device() const noexcept
    {
        return (m_info.vid == 0x4348 || m_info.vid == 0x1A86) && m_info.pid == 0x55E0;
    }

  private:
    Result     transact(std::uint8_t endp_pid, std::uint8_t tog, std::uint32_t timeout_us) noexcept;
    Result     control_transfer(std::uint8_t  request_type,
                                std::uint8_t  request,
                                std::uint16_t value,
                                std::uint16_t index,
                                const void*   out,
                                std::uint16_t out_length,
                                void*         in,
                                std::uint16_t in_length) noexcept;
    Result     get_descriptor(std::uint8_t type, std::uint8_t index, void* buffer, std::uint16_t length) noexcept;
    Result     set_address(std::uint8_t address) noexcept;
    Result     set_configuration(std::uint8_t value) noexcept;
    bool       wait_attached(std::uint32_t timeout_ms) noexcept;
    void       bus_reset() noexcept;
    void       pins_as_gpio(bool dplus_high) noexcept;
    void       release_pins(bool pull_down) noexcept;
    void       power_cycle_floating(std::uint32_t delay_ms) noexcept;
    void       parse_config(const std::uint8_t* config, std::uint16_t length) noexcept;

    DeviceInfo m_info;

    alignas(4) std::uint8_t m_rx[64]{};
    alignas(4) std::uint8_t m_tx[64]{};
    alignas(4) std::uint8_t m_config[192]{};

    std::uint8_t m_ep0_size = 8;
    std::uint8_t m_rx_ctrl  = usb2reg::kUepAutoTog;
    std::uint8_t m_tx_ctrl  = 0;
};

} // namespace ch582::usbhost
