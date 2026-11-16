#include "ch582/usb/host.hpp"

#include <cstring>

#include "ch582/platform/clock.hpp"

namespace ch582::usbhost
{
namespace
{

using usb2reg::kPidAck;
using usb2reg::kPidData0;
using usb2reg::kPidData1;
using usb2reg::kPidIn;
using usb2reg::kPidNak;
using usb2reg::kPidOut;
using usb2reg::kPidSetup;
using usb2reg::kPidStall;

inline void delay_us(std::uint32_t us) noexcept
{
    systick::delay(us * clock::kTicksPerUs);
}

inline void delay_ms(std::uint32_t ms) noexcept
{
    systick::delay(ms * clock::kTicksPerMs);
}

} // namespace

void Host::vbus(bool on) noexcept
{
    gpio::pb::dir() |= gpio::kPin4;
    if (on)
    {
        gpio::pb::out() |= gpio::kPin4;
    }
    else
    {
        gpio::pb::out() &= ~gpio::kPin4;
    }
}

void Host::release_pins(bool pull_down) noexcept
{
    usb2reg::int_en()    = 0;
    usb2reg::ctrl()      = 0;
    usb2reg::host_ctrl() = usb2reg::kUhPdDis;

    sysreg::pin_analog_ie() &= static_cast<std::uint16_t>(~(sysreg::kPinUsb2Ie | sysreg::kPinUsb2DpPu));
    sysreg::pin_alternate() &= static_cast<std::uint16_t>(~sysreg::kPinUart1);

    gpio::pb::pu() &= ~(gpio::kPin12 | gpio::kPin13);
    gpio::pb::dir() &= ~(gpio::kPin12 | gpio::kPin13);
    if (pull_down)
    {
        // D- is only weakly pulled low so it cannot fight the Dongle's UART TX
        // if the target runs its application instead of the ISP bootloader.
        gpio::pb::pd_drv() |= (gpio::kPin12 | gpio::kPin13);
    }
    else
    {
        gpio::pb::pd_drv() &= ~(gpio::kPin12 | gpio::kPin13);
    }
}

void Host::pins_as_gpio(bool dplus_high) noexcept
{
    release_pins(dplus_high);
    if (dplus_high)
    {
        // The bootloader samples D+ at power-on; drive it firmly high.
        gpio::pb::pd_drv() &= ~gpio::kPin13;
        gpio::pb::out() |= gpio::kPin13;
        gpio::pb::dir() |= gpio::kPin13;
    }
}

void Host::init() noexcept
{
    sysreg::enable_usb_clock();
    usb2reg::int_en()       = 0;
    usb2reg::ctrl()         = usb2reg::kUcHostMode;
    usb2reg::host_ctrl()    = 0;
    usb2reg::dev_addr()     = 0;

    usb2reg::ep2_3_mod()    = usb2reg::kUhEpTxEn | usb2reg::kUhEpRxEn;
    usb2reg::host_rx_dma()  = static_cast<std::uint16_t>(reinterpret_cast<std::uintptr_t>(&m_rx[0]));
    usb2reg::host_tx_dma()  = static_cast<std::uint16_t>(reinterpret_cast<std::uintptr_t>(&m_tx[0]));

    usb2reg::host_rx_ctrl() = 0;
    usb2reg::host_tx_ctrl() = 0;
    usb2reg::host_ep_pid()  = 0;

    usb2reg::ctrl()         = usb2reg::kUcHostMode | usb2reg::kUcIntBusy | usb2reg::kUcDmaEn;

    sysreg::pin_alternate() &= static_cast<std::uint16_t>(~sysreg::kPinUart1);
    gpio::pb::dir() &= ~(gpio::kPin12 | gpio::kPin13);
    gpio::pb::pu() &= ~(gpio::kPin12 | gpio::kPin13);
    gpio::pb::pd_drv() &= ~(gpio::kPin12 | gpio::kPin13);
    sysreg::pin_analog_ie() |= sysreg::kPinUsb2Ie;

    usb2reg::host_setup() = usb2reg::kUhSofEn;
    usb2reg::int_fg()     = 0xFF;
    usb2reg::int_en()     = usb2reg::kUieTransfer | usb2reg::kUieDetect;
    usb2reg::host_ctrl()  = 0;

    m_ep0_size            = 8;
    m_rx_ctrl             = usb2reg::kUepAutoTog;
    m_tx_ctrl             = 0;
    m_info                = DeviceInfo{};
}

void Host::disable() noexcept
{
    release_pins(false);
    m_info = DeviceInfo{};
}

void Host::power_off() noexcept
{
    vbus(false);
    disable();

    // Give the target's supply time to collapse. The data lines are left as
    // inputs so an externally powered target cannot be loaded by the Flasher.
    delay_ms(120);
}

void Host::power_cycle_isp() noexcept
{
    power_off();
    pins_as_gpio(true);
    vbus(true);
    delay_ms(100);

    // Release D+ and take over as a USB host. The bootloader sampled D+ high
    // at power-on and stays in ISP mode.
    gpio::pb::dir() &= ~(gpio::kPin12 | gpio::kPin13);
    init();
    delay_ms(80);
}

void Host::power_cycle_master() noexcept
{
    power_off();

    // Host mode already enables the 15 kOhm pulldowns, so the Dongle samples
    // both data lines low and selects master mode.
    init();
    vbus(true);
    delay_ms(120);
}

void Host::power_cycle_floating(std::uint32_t hold_ms) noexcept
{
    power_off();

    // Leave both lines as floating inputs. The Dongle's own pull-ups make the
    // role sample high, so the freshly flashed application starts as a slave
    // and can be reached through the USB-A UART bridge.
    vbus(true);
    delay_ms(hold_ms);
}

void Host::power_cycle_app() noexcept
{
    power_cycle_floating(30);
}

void Host::power_cycle_slave() noexcept
{
    power_cycle_floating(80);
}

bool Host::attached() const noexcept
{
    return (usb2reg::misc_status() & usb2reg::kUmsDeviceAttach) != 0;
}

void Host::bus_reset() noexcept
{
    usb2reg::ctrl() &= static_cast<std::uint8_t>(~usb2reg::kUcLowSpeed);
    usb2reg::host_ctrl() &= ~usb2reg::kUhPortEn;
    usb2reg::host_ctrl() =
        static_cast<std::uint8_t>((usb2reg::host_ctrl() & ~usb2reg::kUhLowSpeed) | usb2reg::kUhBusReset);
    delay_ms(15);
    usb2reg::host_ctrl() &= ~usb2reg::kUhBusReset;
    delay_us(250);
    usb2reg::int_fg() = usb2reg::kUifDetect;
}

Result Host::transact(std::uint8_t endp_pid, std::uint8_t tog, std::uint32_t timeout_us) noexcept
{
    usb2reg::host_rx_ctrl()      = tog;
    usb2reg::host_tx_ctrl()      = tog;

    const std::uint32_t ticks    = (timeout_us < 20u ? 20u : timeout_us) * clock::kTicksPerUs;
    const std::uint32_t deadline = systick::now() + ticks;

    for (;;)
    {
        usb2reg::host_ep_pid() = endp_pid;
        usb2reg::int_fg()      = usb2reg::kUifTransfer;

        while ((usb2reg::int_fg() & usb2reg::kUifTransfer) == 0)
        {
            if ((usb2reg::int_fg() & usb2reg::kUifDetect) != 0)
            {
                usb2reg::int_fg() = usb2reg::kUifDetect;
                if (!attached())
                {
                    usb2reg::host_ep_pid() = 0;
                    return Result::disconnected;
                }
            }
            if (systick::expired(deadline))
            {
                usb2reg::host_ep_pid() = 0;

                for (std::uint32_t grace = 0; grace < 20; ++grace)
                {
                    if ((usb2reg::int_fg() & usb2reg::kUifTransfer) != 0)
                    {
                        break;
                    }
                    delay_us(10);
                }
                if ((usb2reg::int_fg() & usb2reg::kUifTransfer) == 0)
                {
                    return Result::timeout;
                }

                break;
            }
        }

        usb2reg::host_ep_pid() = 0;

        if ((usb2reg::int_fg() & usb2reg::kUifDetect) != 0)
        {
            usb2reg::int_fg() = usb2reg::kUifDetect;
            if (!attached())
            {
                return Result::disconnected;
            }
        }

        const std::uint8_t status = usb2reg::int_st();
        if ((status & usb2reg::kUisToggleOk) != 0)
        {
            return Result::ok;
        }

        const std::uint8_t response = status & usb2reg::kMaskUisHRes;
        if (response == kPidStall)
        {
            return Result::stall;
        }
        if (response == kPidNak)
        {
            if (systick::expired(deadline))
            {
                return Result::nak;
            }
            delay_us(20);
            continue;
        }

        const std::uint8_t token = static_cast<std::uint8_t>(endp_pid >> 4);
        if (token == kPidIn)
        {
            if (response == kPidData0 || response == kPidData1)
            {
                // Data toggle mismatch: discard and retry.
                if (systick::expired(deadline))
                {
                    return Result::timeout;
                }
                delay_us(20);
                continue;
            }
            if (response != 0)
            {
                return Result::error;
            }
        }
        else
        {
            if (response == kPidAck)
            {
                return Result::ok;
            }
            if (response != 0)
            {
                return Result::error;
            }
        }

        if (systick::expired(deadline))
        {
            return Result::timeout;
        }
        delay_us(20);
    }
}

Result Host::control_transfer(std::uint8_t  request_type,
                              std::uint8_t  request,
                              std::uint16_t value,
                              std::uint16_t index,
                              const void*   out,
                              std::uint16_t out_length,
                              void*         in,
                              std::uint16_t in_length) noexcept
{
    m_tx[0]                          = request_type;
    m_tx[1]                          = request;
    m_tx[2]                          = static_cast<std::uint8_t>(value & 0xFFu);
    m_tx[3]                          = static_cast<std::uint8_t>(value >> 8);
    m_tx[4]                          = static_cast<std::uint8_t>(index & 0xFFu);
    m_tx[5]                          = static_cast<std::uint8_t>(index >> 8);
    const std::uint16_t setup_length = in_length != 0 ? in_length : out_length;
    m_tx[6]                          = static_cast<std::uint8_t>(setup_length & 0xFFu);
    m_tx[7]                          = static_cast<std::uint8_t>(setup_length >> 8);

    usb2reg::host_tx_len()           = 8;
    Result x                         = transact(static_cast<std::uint8_t>((kPidSetup << 4) | 0), 0, 200000);
    if (x != Result::ok)
    {
        return x;
    }

    usb2reg::host_rx_ctrl() = static_cast<std::uint8_t>(usb2reg::kUepRTog | usb2reg::kUepTTog | usb2reg::kUepAutoTog);
    usb2reg::host_tx_ctrl() = usb2reg::host_rx_ctrl();

    if (in != nullptr && in_length != 0)
    {
        auto*         dst       = static_cast<std::uint8_t*>(in);
        std::uint16_t remaining = in_length;

        while (remaining != 0)
        {
            x = transact(static_cast<std::uint8_t>((kPidIn << 4) | 0), usb2reg::host_rx_ctrl(), 200000);
            if (x != Result::ok)
            {
                return x;
            }

            const std::uint8_t received = usb2reg::rx_len();
            if (received == 0)
            {
                break;
            }
            const std::uint16_t copy = received < remaining ? received : remaining;
            std::memcpy(dst, m_rx, copy);
            dst += copy;
            remaining -= copy;

            if (received < m_ep0_size)
            {
                break;
            }
        }

        usb2reg::host_tx_len() = 0;
        x                      = transact(static_cast<std::uint8_t>((kPidOut << 4) | 0),
                     static_cast<std::uint8_t>(usb2reg::kUepRTog | usb2reg::kUepTTog),
                     200000);
        if (x != Result::ok)
        {
            return x;
        }
    }
    else if (out != nullptr && out_length != 0)
    {
        auto*         src       = static_cast<const std::uint8_t*>(out);
        std::uint16_t remaining = out_length;

        while (remaining != 0)
        {
            const std::uint16_t chunk = remaining >= m_ep0_size ? m_ep0_size : remaining;
            std::memcpy(m_tx, src, chunk);
            usb2reg::host_tx_len() = static_cast<std::uint8_t>(chunk);

            x = transact(static_cast<std::uint8_t>((kPidOut << 4) | 0), usb2reg::host_tx_ctrl(), 200000);
            if (x != Result::ok)
            {
                return x;
            }

            src += chunk;
            remaining -= chunk;
        }

        usb2reg::host_tx_len() = 1;
        x                      = transact(static_cast<std::uint8_t>((kPidIn << 4) | 0),
                     static_cast<std::uint8_t>(usb2reg::kUepRTog | usb2reg::kUepTTog),
                     200000);
        if (x != Result::ok)
        {
            return x;
        }
    }
    else
    {
        usb2reg::host_tx_len() = 1;
        x                      = transact(static_cast<std::uint8_t>((kPidIn << 4) | 0),
                     static_cast<std::uint8_t>(usb2reg::kUepRTog | usb2reg::kUepTTog),
                     200000);
        if (x != Result::ok)
        {
            return x;
        }
    }

    // The bulk endpoint toggle state lives in m_rx_ctrl/m_tx_ctrl. Leave it
    // untouched here so a SET_LINE_CODING performed while the bridge is
    // running does not reset the data toggles.
    return Result::ok;
}

Result Host::get_descriptor(std::uint8_t type, std::uint8_t index, void* buffer, std::uint16_t length) noexcept
{
    return control_transfer(0x80, 0x06, static_cast<std::uint16_t>((type << 8) | index), 0, nullptr, 0, buffer, length);
}

Result Host::set_address(std::uint8_t address) noexcept
{
    return control_transfer(0x00, 0x05, address, 0, nullptr, 0, nullptr, 0);
}

Result Host::set_configuration(std::uint8_t value) noexcept
{
    const Result result = control_transfer(0x00, 0x09, value, 0, nullptr, 0, nullptr, 0);
    if (result == Result::ok)
    {
        m_rx_ctrl               = usb2reg::kUepAutoTog;
        m_tx_ctrl               = 0;
        usb2reg::host_rx_ctrl() = m_rx_ctrl;
        usb2reg::host_tx_ctrl() = m_tx_ctrl;
    }
    return result;
}

void Host::parse_config(const std::uint8_t* config, std::uint16_t length) noexcept
{
    m_info.bulk_in      = 0;
    m_info.bulk_out     = 0;
    m_info.has_cdc      = false;
    m_info.cdc_comm_itf = 0;

    if (length >= 9)
    {
        m_info.config_value = config[5] != 0 ? config[5] : 1;
    }

    for (std::uint16_t i = 0; i + 1 < length;)
    {
        const std::uint8_t descriptor_length = config[i];
        const std::uint8_t descriptor_type   = config[i + 1];
        if (descriptor_length < 2 || static_cast<std::uint16_t>(i + descriptor_length) > length)
        {
            break;
        }

        if (descriptor_type == 0x04 && descriptor_length >= 9)
        {
            const std::uint8_t interface_class    = config[i + 5];
            const std::uint8_t interface_subclass = config[i + 6];
            if (interface_class == 0x02 && interface_subclass == 0x02)
            {
                m_info.has_cdc      = true;
                m_info.cdc_comm_itf = config[i + 2];
            }
        }
        else if (descriptor_type == 0x05 && descriptor_length >= 7)
        {
            const std::uint8_t endpoint_address = config[i + 2];
            const std::uint8_t attributes       = config[i + 3];
            if ((attributes & 0x03u) == 0x02u)
            {
                if ((endpoint_address & 0x80u) != 0)
                {
                    if (m_info.bulk_in == 0)
                    {
                        m_info.bulk_in = endpoint_address;
                    }
                }
                else
                {
                    if (m_info.bulk_out == 0)
                    {
                        m_info.bulk_out = endpoint_address;
                    }
                }
            }
        }

        i = static_cast<std::uint16_t>(i + descriptor_length);
    }
}

bool Host::wait_attached(std::uint32_t timeout_ms) noexcept
{
    const std::uint32_t deadline = systick::now() + timeout_ms * clock::kTicksPerMs;
    while (!attached())
    {
        if (systick::expired(deadline))
        {
            return false;
        }
        delay_us(200);
    }
    return true;
}

Result Host::enumerate(DeviceInfo& info, std::uint32_t timeout_ms) noexcept
{
    if (!wait_attached(timeout_ms))
    {
        return Result::disconnected;
    }

    bus_reset();

    // Bus reset can make the target detach and re-attach while its USB device
    // stack initializes. Wait for a stable attach before enumerating.
    if (!wait_attached(600))
    {
        return Result::disconnected;
    }
    delay_ms(20);

    usb2reg::host_ctrl() |= usb2reg::kUhPortEn;
    delay_ms(100);
    if (!attached())
    {
        return Result::disconnected;
    }

    m_ep0_size          = 8;
    usb2reg::dev_addr() = 0;

    Result result       = get_descriptor(0x01, 0, m_config, 18);
    if (result != Result::ok)
    {
        return result;
    }

    m_info.vid = static_cast<std::uint16_t>(m_config[8] | (m_config[9] << 8));
    m_info.pid = static_cast<std::uint16_t>(m_config[10] | (m_config[11] << 8));
    m_ep0_size = m_config[7] != 0 ? m_config[7] : 8;

    result     = set_address(2);
    if (result != Result::ok)
    {
        return result;
    }

    usb2reg::dev_addr() = 2;
    delay_ms(10);

    result = get_descriptor(0x02, 0, m_config, 9);
    if (result != Result::ok)
    {
        return result;
    }

    const std::uint16_t total = static_cast<std::uint16_t>(m_config[2] | (m_config[3] << 8));
    if (total < 9 || total > sizeof(m_config))
    {
        return Result::error;
    }

    result = get_descriptor(0x02, 0, m_config, total);
    if (result != Result::ok)
    {
        return result;
    }

    parse_config(m_config, total);

    result = set_configuration(m_info.config_value);
    if (result != Result::ok)
    {
        return result;
    }

    info = m_info;
    return Result::ok;
}

Result Host::set_line_coding(std::uint8_t itf, const serial::LineCoding& coding) noexcept
{
    if (!m_info.has_cdc)
    {
        return Result::error;
    }
    return control_transfer(0x21, 0x20, 0, itf, &coding, sizeof(coding), nullptr, 0);
}

Result Host::set_control_line_state(std::uint8_t itf, std::uint16_t state) noexcept
{
    if (!m_info.has_cdc)
    {
        return Result::error;
    }
    return control_transfer(0x21, 0x22, state, itf, nullptr, 0, nullptr, 0);
}

Result Host::bulk_out(std::uint8_t ep, const void* data, std::uint8_t length, std::uint32_t timeout_us) noexcept
{
    if (length > sizeof(m_tx))
    {
        return Result::error;
    }

    if (length != 0)
    {
        std::memcpy(m_tx, data, length);
    }
    usb2reg::host_tx_len() = length;

    const Result x         = transact(static_cast<std::uint8_t>((kPidOut << 4) | (ep & 0x0Fu)), m_tx_ctrl, timeout_us);
    if (x == Result::ok)
    {
        m_tx_ctrl               = static_cast<std::uint8_t>(m_tx_ctrl ^ usb2reg::kUepTTog);
        usb2reg::host_tx_ctrl() = m_tx_ctrl;
        usb2reg::host_rx_ctrl() = m_rx_ctrl;
        return Result::ok;
    }

    usb2reg::host_rx_ctrl() = m_rx_ctrl;
    usb2reg::host_tx_ctrl() = m_tx_ctrl;
    return x;
}

Result Host::bulk_in(std::uint8_t ep, void* data, std::uint8_t& length, std::uint32_t timeout_us) noexcept
{
    const Result x = transact(static_cast<std::uint8_t>((kPidIn << 4) | (ep & 0x0Fu)), m_rx_ctrl, timeout_us);
    if (x == Result::ok)
    {
        m_rx_ctrl               = usb2reg::host_rx_ctrl();
        usb2reg::host_tx_ctrl() = m_tx_ctrl;

        length                  = usb2reg::rx_len();
        if (length > sizeof(m_rx))
        {
            length = sizeof(m_rx);
        }
        if (length != 0)
        {
            std::memcpy(data, m_rx, length);
        }
        return Result::ok;
    }

    length                  = 0;
    usb2reg::host_rx_ctrl() = m_rx_ctrl;
    usb2reg::host_tx_ctrl() = m_tx_ctrl;
    return x;
}

} // namespace ch582::usbhost
