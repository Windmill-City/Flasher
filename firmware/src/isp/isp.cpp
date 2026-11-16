#include "ch582/isp/isp.hpp"

#include <cstring>

#include "ch582/platform/clock.hpp"
#include "ch582/util/bytes.hpp"

namespace ch582::isp
{
namespace
{

constexpr std::uint8_t  kCmdIdentify    = 0xA1;
constexpr std::uint8_t  kCmdIspEnd      = 0xA2;
constexpr std::uint8_t  kCmdIspKey      = 0xA3;
constexpr std::uint8_t  kCmdErase       = 0xA4;
constexpr std::uint8_t  kCmdProgram     = 0xA5;
constexpr std::uint8_t  kCmdVerify      = 0xA6;
constexpr std::uint8_t  kCmdReadConfig  = 0xA7;
constexpr std::uint8_t  kCmdWriteConfig = 0xA8;

constexpr std::uint8_t  kConfigMaskAll  = 0x1F;
constexpr std::uint8_t  kCfgCodeUnlock  = 0xA5;
constexpr std::uint32_t kCfgDebugEn     = 1u << 4;
constexpr std::uint8_t  kChipCh570      = 0x70;
constexpr std::uint8_t  kChipCh572      = 0x72;

} // namespace

void Isp::update_key() noexcept
{
    std::uint8_t sum = 0;
    for (std::size_t i = 0; i < 8; ++i)
    {
        sum = static_cast<std::uint8_t>(sum + m_info.uid[i]);
    }

    for (std::size_t i = 0; i < 7; ++i)
    {
        m_key[i] = sum;
    }
    m_key[7] = static_cast<std::uint8_t>(sum + m_info.chip_id);
}

bool Isp::read_response(std::uint8_t* response, std::uint8_t& response_length, std::uint32_t timeout_ms) noexcept
{
    const std::uint32_t deadline = systick::now() + timeout_ms * clock::kTicksPerMs;

    for (;;)
    {
        std::uint8_t received = 0;
        const auto   result =
            m_host.bulk_in(m_host.info().bulk_in, response, received, static_cast<std::uint32_t>(20u * 1000u));

        if (result == usbhost::Result::ok)
        {
            if (received < 4)
            {
                return false;
            }

            const std::uint16_t payload = static_cast<std::uint16_t>(response[2] | (response[3] << 8));
            const std::uint16_t total   = static_cast<std::uint16_t>(4 + payload);
            if (total > received)
            {
                return false;
            }

            response_length = static_cast<std::uint8_t>(total);
            return true;
        }

        if (result == usbhost::Result::disconnected || result == usbhost::Result::stall)
        {
            return false;
        }

        if (systick::expired(deadline))
        {
            return false;
        }
    }
}

bool Isp::transfer(const std::uint8_t* command,
                   std::uint8_t        length,
                   std::uint8_t*       response,
                   std::uint8_t&       response_length,
                   std::uint32_t       timeout_ms) noexcept
{
    if (!m_host.attached() || m_host.info().bulk_out == 0 || m_host.info().bulk_in == 0)
    {
        return false;
    }

    // The bootloader accepts one 64-byte command packet at a time.
    if (length > 64)
    {
        return false;
    }

    const auto send = m_host.bulk_out(m_host.info().bulk_out, command, length, 500000);
    if (send != usbhost::Result::ok)
    {
        return false;
    }

    if (!read_response(response, response_length, timeout_ms))
    {
        return false;
    }

    return response[0] == command[0];
}

bool Isp::send_key(std::uint8_t* response, std::uint8_t& response_length) noexcept
{
    // ISP_KEY with an all-zero seed. The response checksum is the sum of the
    // XOR key bytes that the bootloader derived from the UID.
    std::uint8_t isp_key[33];
    isp_key[0] = kCmdIspKey;
    isp_key[1] = 0x1E;
    isp_key[2] = 0x00;
    std::memset(&isp_key[3], 0, 30);

    if (!transfer(isp_key, sizeof(isp_key), response, response_length, 1000) || response_length < 5)
    {
        return false;
    }

    std::uint8_t key_sum = 0;
    for (std::uint8_t value : m_key)
    {
        key_sum = static_cast<std::uint8_t>(key_sum + value);
    }
    return response[4] == key_sum;
}

bool Isp::identify(Info& info) noexcept
{
    m_active       = false;
    m_write_closed = false;
    m_identified   = false;
    m_next_address = 0;
    m_padding      = 0xA5;
    m_info         = Info{};

    if (m_host.info().bulk_out == 0 || m_host.info().bulk_in == 0)
    {
        return false;
    }

    std::uint8_t response[64];
    std::uint8_t response_length = 0;

    std::uint8_t identify[21];
    identify[0] = kCmdIdentify;
    identify[1] = 0x12;
    identify[2] = 0x00;
    identify[3] = 0x00;
    identify[4] = 0x00;
    std::memcpy(&identify[5], "MCU ISP & WCH.CN", 16);

    if (!transfer(identify, sizeof(identify), response, response_length, 1000) || response_length < 6)
    {
        return false;
    }
    m_info.chip_id              = response[4];
    m_info.device_type          = response[5];

    // READ_CONFIG(all): the raw config block starts at payload offset 2 and
    // the UID at payload offset 18.
    std::uint8_t read_config[5] = {kCmdReadConfig, 0x02, 0x00, kConfigMaskAll, 0x00};
    if (!transfer(read_config, sizeof(read_config), response, response_length, 1000) || response_length < 30)
    {
        return false;
    }
    std::memcpy(m_config, &response[4 + 2], sizeof(m_config));
    std::memcpy(m_info.uid, &response[4 + 18], sizeof(m_info.uid));

    update_key();
    m_identified = true;
    info         = m_info;
    return true;
}

bool Isp::unlock_required() const noexcept
{
    if (!m_identified)
    {
        return false;
    }

    // CH570/CH572 keep the code-flash protection marker in the first config
    // word; an erased marker makes every PROGRAM fail.
    if (m_info.chip_id == kChipCh570 || m_info.chip_id == kChipCh572)
    {
        return m_config[0] != kCfgCodeUnlock;
    }

    // Other CH58x parts block writes while CFG_DEBUG_EN is set.
    return (read_u32(&m_config[8]) & kCfgDebugEn) != 0;
}

bool Isp::unlock() noexcept
{
    if (!m_identified)
    {
        return false;
    }

    std::uint8_t response[64];
    std::uint8_t response_length = 0;
    if (!send_key(response, response_length))
    {
        return false;
    }

    std::uint8_t config[12];
    std::memcpy(config, m_config, sizeof(config));

    if (m_info.chip_id == kChipCh570 || m_info.chip_id == kChipCh572)
    {
        config[0] = kCfgCodeUnlock;
        config[1] = 0x5A;
    }
    else
    {
        write_u32(&config[8], read_u32(&config[8]) & ~kCfgDebugEn);
    }

    std::uint8_t frame[5 + sizeof(config)];
    frame[0] = kCmdWriteConfig;
    frame[1] = static_cast<std::uint8_t>(2 + sizeof(config));
    frame[2] = 0x00;
    frame[3] = 0x07;
    frame[4] = 0x00;
    std::memcpy(&frame[5], config, sizeof(config));

    if (!transfer(frame, sizeof(frame), response, response_length, 10000))
    {
        return false;
    }

    // The target reboots before the bootloader can answer.
    std::uint8_t end[4] = {kCmdIspEnd, 0x01, 0x00, 0x01};
    (void)transfer(end, sizeof(end), response, response_length, 500);

    m_active       = false;
    m_write_closed = false;
    m_identified   = false;
    return true;
}

bool Isp::begin(std::uint32_t image_size, Info& info) noexcept
{
    if (image_size > 240u * 1024u)
    {
        return false;
    }

    if (!m_identified && !identify(info))
    {
        return false;
    }

    m_active              = false;
    m_write_closed        = false;
    m_next_address        = 0;
    m_padding             = 0xA5;

    std::uint32_t sectors = (image_size + 1023u) / 1024u;
    if (sectors < 8u)
    {
        sectors = 8u;
    }
    if (sectors > 240u)
    {
        sectors = 240u;
    }
    m_info.erase_sectors = sectors;

    std::uint8_t erase[7];
    erase[0] = kCmdErase;
    erase[1] = 0x04;
    erase[2] = 0x00;
    write_u32(&erase[3], sectors);

    std::uint8_t response[64];
    std::uint8_t response_length = 0;

    if (!transfer(erase, sizeof(erase), response, response_length, 10000))
    {
        return false;
    }

    // The key must be sent after the erase; erase resets the bootloader state.
    if (!send_key(response, response_length))
    {
        return false;
    }

    m_active = true;
    info     = m_info;
    return true;
}

bool Isp::send_code(std::uint8_t        command,
                    std::uint32_t       address,
                    const std::uint8_t* data,
                    std::uint8_t        length,
                    std::uint8_t*       response,
                    std::uint8_t&       response_length) noexcept
{
    std::uint8_t frame[64];
    frame[0] = command;
    frame[1] = static_cast<std::uint8_t>(5 + length);
    frame[2] = 0x00;
    write_u32(&frame[3], address);
    frame[7] = m_padding++;

    for (std::uint8_t i = 0; i < length; ++i)
    {
        frame[8 + i] = static_cast<std::uint8_t>(data[i] ^ m_key[i & 7u]);
    }

    return transfer(frame, static_cast<std::uint8_t>(8 + length), response, response_length, 1000);
}

bool Isp::program(std::uint32_t address, const std::uint8_t* data, std::uint8_t length) noexcept
{
    if (!m_active || length > 56)
    {
        return false;
    }

    std::uint8_t response[64];
    std::uint8_t response_length = 0;
    if (!send_code(kCmdProgram, address, data, length, response, response_length))
    {
        return false;
    }

    m_next_address = address + length;
    if (length != 0)
    {
        m_write_closed = false;
    }
    return true;
}

bool Isp::verify(std::uint32_t address, const std::uint8_t* data, std::uint8_t length) noexcept
{
    if (!m_active || length > 56)
    {
        return false;
    }

    if (!close_write_session(true))
    {
        return false;
    }

    std::uint8_t response[64];
    std::uint8_t response_length = 0;
    if (!send_code(kCmdVerify, address, data, length, response, response_length))
    {
        return false;
    }

    return response_length >= 5 && response[4] == 0x00;
}

bool Isp::close_write_session(bool rekey) noexcept
{
    if (m_write_closed)
    {
        return true;
    }

    // The bootloader expects a final empty program at the end address before
    // the data is committed.
    if (!program(m_next_address, nullptr, 0))
    {
        return false;
    }

    // A separate VERIFY phase needs the XOR key re-armed after the write
    // session is closed (this matches the WCHISP host sequence).
    if (rekey)
    {
        systick::delay(5u * clock::kTicksPerMs);

        std::uint8_t response[64];
        std::uint8_t response_length = 0;
        if (!send_key(response, response_length))
        {
            return false;
        }
    }

    m_write_closed = true;
    return true;
}

bool Isp::finish() noexcept
{
    if (!m_active)
    {
        return false;
    }

    // The bootloader expects a final empty program at the end address.
    if (!close_write_session(false))
    {
        return false;
    }

    std::uint8_t isp_end[4] = {kCmdIspEnd, 0x01, 0x00, 0x00};
    std::uint8_t response[64];
    std::uint8_t response_length = 0;

    // The device may reboot before the response reaches us.
    (void)transfer(isp_end, sizeof(isp_end), response, response_length, 500);

    m_active     = false;
    m_identified = false;
    return true;
}

} // namespace ch582::isp
