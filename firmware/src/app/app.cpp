#include "ch582/app/app.hpp"

#include <cstring>

#include "ch582/platform/clock.hpp"
#include "ch582/util/bytes.hpp"

namespace ch582::app
{
namespace
{

constexpr std::uint8_t kRequestMagic0  = 0xAA;
constexpr std::uint8_t kRequestMagic1  = 0x55;
constexpr std::uint8_t kResponseMagic0 = 0x55;
constexpr std::uint8_t kResponseMagic1 = 0xAA;

std::uint8_t           crc8(const std::uint8_t* data, std::size_t length) noexcept
{
    std::uint8_t crc = 0;
    for (std::size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 0x80u) != 0 ? static_cast<std::uint8_t>((crc << 1) ^ 0x07u)
                                     : static_cast<std::uint8_t>(crc << 1);
        }
    }
    return crc;
}

} // namespace

void App::init() noexcept
{
    m_usb.set_tx_buffer(&m_tx);
    m_seen_coding_generation = m_usb.line_coding_generation();
    m_seen_control_state     = m_usb.control_state();
    reset_parser();
}

void App::reset_parser() noexcept
{
    m_frame_length = 0;
}

void App::feed(std::uint8_t byte) noexcept
{
    if (m_frame_length == 0)
    {
        if (byte == kRequestMagic0)
        {
            m_frame[m_frame_length++] = byte;
        }
        return;
    }

    if (m_frame_length == 1)
    {
        if (byte == kRequestMagic1)
        {
            m_frame[m_frame_length++] = byte;
        }
        else
        {
            m_frame_length = 0;
            if (byte == kRequestMagic0)
            {
                m_frame[m_frame_length++] = byte;
            }
        }
        return;
    }

    if (m_frame_length >= sizeof(m_frame))
    {
        reset_parser();
        return;
    }

    m_frame[m_frame_length++] = byte;

    if (m_frame_length >= 4)
    {
        const std::uint8_t payload_length = m_frame[3];
        if (payload_length > 64)
        {
            reset_parser();
            return;
        }

        const std::size_t total = 5u + payload_length;
        if (total > sizeof(m_frame))
        {
            reset_parser();
            return;
        }
        if (m_frame_length == total)
        {
            process_frame();
            reset_parser();
        }
    }
}

void App::process_input() noexcept
{
    std::uint8_t buffer[64];
    std::size_t  length = 0;
    while ((length = m_usb.read_host(buffer, sizeof(buffer))) != 0)
    {
        for (std::size_t i = 0; i < length; ++i)
        {
            feed(buffer[i]);
        }
    }
}

void App::send_response(std::uint8_t command, const void* payload, std::uint8_t length) noexcept
{
    if (length > 64)
    {
        return;
    }

    std::uint8_t frame[72];
    frame[0] = kResponseMagic0;
    frame[1] = kResponseMagic1;
    frame[2] = command;
    frame[3] = length;
    if (length != 0 && payload != nullptr)
    {
        std::memcpy(&frame[4], payload, length);
    }
    frame[4 + length] = crc8(&frame[2], static_cast<std::size_t>(2 + length));
    m_tx.write(frame, static_cast<std::size_t>(5 + length));
}

void App::send_status(std::uint8_t command, std::uint8_t status) noexcept
{
    send_response(command, &status, 1);
}

void App::process_frame() noexcept
{
    const std::uint8_t command        = m_frame[2];
    const std::uint8_t payload_length = m_frame[3];
    const std::uint8_t expected_crc   = crc8(&m_frame[2], static_cast<std::size_t>(2 + payload_length));

    if (m_frame[4 + payload_length] != expected_crc)
    {
        return;
    }

    const std::uint8_t* payload = &m_frame[4];

    switch (command)
    {
    case kCmdPing:
    case kCmdInfo:
    {
        std::uint8_t response[9];
        response[0] = kStatusOk;
        response[1] = 1; // protocol major
        response[2] = 1; // protocol minor
        response[3] = m_mode;
        response[4] = target_kind();
        write_vid_pid(&response[5]);
        send_response(command, response, sizeof(response));
        break;
    }

    case kCmdSetMode:
    {
        if (payload_length < 1)
        {
            send_status(command, kStatusBadArg);
            break;
        }

        const std::uint8_t mode = payload[0];
        if (mode == kModeIdle)
        {
            reset_session();
            send_status(command, kStatusOk);
        }
        else if (mode == kModeMaster)
        {
            const std::uint8_t status = enter_master();
            if (status != kStatusOk)
            {
                send_status(command, status);
                break;
            }

            std::uint8_t response[7];
            response[0] = kStatusOk;
            response[1] = m_mode;
            response[2] = target_kind();
            write_vid_pid(&response[3]);
            send_response(command, response, sizeof(response));
        }
        else if (mode == kModeSlave)
        {
            std::uint32_t baud = 115200;
            if (payload_length >= 5)
            {
                baud = read_u32(&payload[1]);
            }

            const std::uint8_t status = enter_slave(baud);
            if (status != kStatusOk)
            {
                send_status(command, status);
                break;
            }

            std::uint8_t response[3] = {kStatusOk, m_mode, kTargetUart};
            send_response(command, response, sizeof(response));
        }
        else
        {
            send_status(command, kStatusBadArg);
        }
        break;
    }

    case kCmdPower:
    {
        if (payload_length < 2)
        {
            send_status(command, kStatusBadArg);
            break;
        }

        reset_session();

        if (payload[0] == 0)
        {
            m_host.power_off();
        }
        else if (payload[1] != 0)
        {
            m_host.power_cycle_isp();
        }
        else
        {
            m_host.power_cycle_app();
        }

        send_status(command, kStatusOk);
        break;
    }

    case kCmdIspBegin:
    {
        if (payload_length < 4)
        {
            send_status(command, kStatusBadArg);
            break;
        }

        isp::Isp::Info     info;
        const std::uint8_t status = begin_isp(read_u32(payload), info);
        if (status != kStatusOk)
        {
            send_status(command, status);
            break;
        }

        std::uint8_t response[15];
        response[0] = kStatusOk;
        response[1] = info.chip_id;
        response[2] = info.device_type;
        std::memcpy(&response[3], info.uid, 8);
        write_u32(&response[11], info.erase_sectors);
        send_response(command, response, sizeof(response));
        break;
    }

    case kCmdIspWrite:
    case kCmdIspVerify:
    {
        if (payload_length < 4 || (payload_length - 4) > 56)
        {
            send_status(command, kStatusBadArg);
            break;
        }
        if (!m_isp.active())
        {
            send_status(command, kStatusIsp);
            break;
        }

        const std::uint32_t address = read_u32(payload);
        const std::uint8_t  length  = static_cast<std::uint8_t>(payload_length - 4);
        const bool          ok      = command == kCmdIspWrite ? m_isp.program(address, &payload[4], length)
                                                              : m_isp.verify(address, &payload[4], length);

        if (!ok)
        {
            send_status(command, kStatusIsp);
            break;
        }

        std::uint8_t response[5];
        response[0] = kStatusOk;
        write_u32(&response[1], address);
        send_response(command, response, sizeof(response));
        break;
    }

    case kCmdIspFinish:
    {
        if (!m_isp.active())
        {
            send_status(command, kStatusIsp);
            break;
        }

        const bool finished = m_isp.finish();
        m_host.power_cycle_app();
        m_mode = kModeIdle;
        send_status(command, finished ? kStatusOk : kStatusIsp);
        break;
    }

    default:
        send_status(command, kStatusBadCmd);
        break;
    }
}

std::uint8_t App::status_from(usbhost::Result result) noexcept
{
    if (result == usbhost::Result::ok)
    {
        return kStatusOk;
    }
    if (result == usbhost::Result::disconnected || result == usbhost::Result::timeout)
    {
        return kStatusNoTarget;
    }
    return kStatusUsb;
}

void App::reset_session() noexcept
{
    m_isp.abort();
    stop_bridge();
}

std::uint8_t App::attach(bool expect_isp) noexcept
{
    reset_session();

    if (expect_isp)
    {
        m_host.power_cycle_isp();
    }
    else
    {
        m_host.power_cycle_master();
    }

    usbhost::DeviceInfo info;
    const auto          result = m_host.enumerate(info, 1200);
    if (result != usbhost::Result::ok)
    {
        return status_from(result);
    }

    if (m_host.is_isp_device() != expect_isp)
    {
        return kStatusNoTarget;
    }

    return kStatusOk;
}

std::uint8_t App::enter_isp_host() noexcept
{
    return attach(true);
}

std::uint8_t App::begin_isp(std::uint32_t image_size, isp::Isp::Info& info) noexcept
{
    std::uint8_t status = enter_isp_host();
    if (status != kStatusOk)
    {
        return status;
    }

    if (!m_isp.identify(info))
    {
        return kStatusIsp;
    }

    if (m_isp.unlock_required())
    {
        if (!m_isp.unlock())
        {
            return kStatusIsp;
        }

        status = enter_isp_host();
        if (status != kStatusOk)
        {
            return status;
        }

        if (!m_isp.identify(info) || m_isp.unlock_required())
        {
            return kStatusIsp;
        }
    }

    return m_isp.begin(image_size, info) ? kStatusOk : kStatusIsp;
}

void App::write_vid_pid(std::uint8_t* dst) const noexcept
{
    const auto& info = m_host.info();
    dst[0]           = static_cast<std::uint8_t>(info.vid & 0xFFu);
    dst[1]           = static_cast<std::uint8_t>(info.vid >> 8);
    dst[2]           = static_cast<std::uint8_t>(info.pid & 0xFFu);
    dst[3]           = static_cast<std::uint8_t>(info.pid >> 8);
}

std::uint8_t App::enter_master() noexcept
{
    const std::uint8_t status = attach(false);
    if (status != kStatusOk)
    {
        return status;
    }

    const auto& info = m_host.info();
    if (info.bulk_in == 0 || info.bulk_out == 0)
    {
        return kStatusUnsupported;
    }

    m_bulk_in  = info.bulk_in;
    m_bulk_out = info.bulk_out;
    m_cdc_itf  = info.cdc_comm_itf;

    if (info.has_cdc)
    {
        m_host.set_line_coding(info.cdc_comm_itf, pc_line_coding());
        m_host.set_control_line_state(info.cdc_comm_itf, m_usb.control_state());
    }

    m_seen_coding_generation = m_usb.line_coding_generation();
    m_seen_control_state     = m_usb.control_state();
    m_pending_length         = 0;
    m_mode                   = kModeMaster;
    return kStatusOk;
}

std::uint8_t App::enter_slave(std::uint32_t baud) noexcept
{
    reset_session();

    // Power the target with the data lines floating so its bootloader sees D+
    // low and starts the application. The application's own pull-ups then make
    // the role sample high, selecting slave mode. Initialising the UART before
    // power-up would drive D+ high and trap the target in the ISP bootloader.
    m_host.power_cycle_slave();
    m_uart.init(baud);

    m_seen_coding_generation = m_usb.line_coding_generation();
    m_mode                   = kModeSlave;
    return kStatusOk;
}

void App::stop_bridge() noexcept
{
    if (m_mode == kModeMaster)
    {
        m_host.disable();
    }
    else if (m_mode == kModeSlave)
    {
        m_uart.deinit();
    }

    m_mode           = kModeIdle;
    m_pending_length = 0;
    m_tx.clear();
    reset_parser();
}

serial::LineCoding App::pc_line_coding() const noexcept
{
    CriticalSection guard;
    return m_usb.line_coding();
}

void App::handle_coding_change() noexcept
{
    const std::uint32_t generation = m_usb.line_coding_generation();
    if (generation != m_seen_coding_generation)
    {
        m_seen_coding_generation = generation;
        if (m_mode == kModeSlave && m_uart.active())
        {
            m_uart.configure(pc_line_coding());
        }
        else if (m_mode == kModeMaster && m_host.info().has_cdc)
        {
            m_host.set_line_coding(m_cdc_itf, pc_line_coding());
        }
    }

    const std::uint8_t control_state = m_usb.control_state();
    if (control_state != m_seen_control_state)
    {
        m_seen_control_state = control_state;
        if (m_mode == kModeMaster && m_host.info().has_cdc)
        {
            m_host.set_control_line_state(m_cdc_itf, control_state);
        }
    }
}

void App::bridge_poll() noexcept
{
    if (m_mode == kModeMaster)
    {
        if (!m_host.attached())
        {
            stop_bridge();
            return;
        }

        if (m_pending_length == 0)
        {
            const std::size_t length = m_usb.read_host(m_pending, sizeof(m_pending));
            if (length != 0)
            {
                m_pending_length = static_cast<std::uint8_t>(length);
            }
        }

        if (m_pending_length != 0)
        {
            const auto result = m_host.bulk_out(m_bulk_out, m_pending, m_pending_length, 3000);
            if (result == usbhost::Result::ok)
            {
                m_pending_length = 0;
            }
            else if (result == usbhost::Result::stall || result == usbhost::Result::disconnected)
            {
                stop_bridge();
                return;
            }
        }

        if (m_tx.free() >= 64)
        {
            std::uint8_t buffer[64];
            std::uint8_t length = 0;
            const auto   result = m_host.bulk_in(m_bulk_in, buffer, length, 1500);
            if (result == usbhost::Result::ok)
            {
                if (length != 0)
                {
                    m_tx.write(buffer, length);
                }
            }
            else if (result == usbhost::Result::stall || result == usbhost::Result::disconnected)
            {
                stop_bridge();
            }
        }
    }
    else if (m_mode == kModeSlave)
    {
        std::uint8_t      buffer[64];
        const std::size_t length = m_usb.read_host(buffer, sizeof(buffer));
        if (length != 0)
        {
            m_uart.write(buffer, length);
        }

        while (m_tx.free() >= 64)
        {
            const std::size_t received = m_uart.read(buffer, sizeof(buffer));
            if (received == 0)
            {
                break;
            }
            m_tx.write(buffer, received);
        }
    }
}

std::uint8_t App::target_kind() const noexcept
{
    if (m_mode == kModeSlave)
    {
        return kTargetUart;
    }
    if (m_mode == kModeMaster)
    {
        return m_host.info().has_cdc ? kTargetCdc : kTargetBulk;
    }
    if (m_isp.active())
    {
        return kTargetIsp;
    }
    return kTargetNone;
}

void App::poll() noexcept
{
    m_usb.poll();

    if (m_usb.consume_break())
    {
        stop_bridge();
    }

    handle_coding_change();

    if (m_mode == kModeIdle)
    {
        process_input();
    }
    else
    {
        bridge_poll();
    }

    m_usb.poll();
}

} // namespace ch582::app
