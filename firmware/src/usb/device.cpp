#include "ch582/usb/device.hpp"

#include <algorithm>
#include <cstring>

#include "ch582/usb/descriptors.hpp"

namespace ch582::usb
{

namespace
{

constexpr std::uint8_t kCdcSetCommFeature      = 0x02;
constexpr std::uint8_t kCdcGetCommFeature      = 0x03;
constexpr std::uint8_t kCdcClearCommFeature    = 0x04;
constexpr std::uint8_t kCdcSetLineCoding       = 0x20;
constexpr std::uint8_t kCdcGetLineCoding       = 0x21;
constexpr std::uint8_t kCdcSetControlLineState = 0x22;
constexpr std::uint8_t kCdcSendBreak           = 0x23;

constexpr std::uint8_t kStall                  = 0xFF;
constexpr std::uint8_t kTokenOut               = 0x00;
constexpr std::uint8_t kTokenIn                = 0x20;
constexpr std::uint8_t kToggleOk               = 0x40;

constexpr std::uint8_t kRequestTypeIn          = 0x80;
constexpr std::uint8_t kRequestTypeMask        = 0x60;
constexpr std::uint8_t kRequestTypeStandard    = 0x00;
constexpr std::uint8_t kRequestTypeClass       = 0x20;
constexpr std::uint8_t kRecipientMask          = 0x1F;
constexpr std::uint8_t kRecipientDevice        = 0x00;
constexpr std::uint8_t kRecipientEndpoint      = 0x02;

constexpr std::uint8_t kReqGetStatus           = 0x00;
constexpr std::uint8_t kReqClearFeature        = 0x01;
constexpr std::uint8_t kReqSetAddress          = 0x05;
constexpr std::uint8_t kReqGetDescriptor       = 0x06;
constexpr std::uint8_t kReqGetConfiguration    = 0x08;
constexpr std::uint8_t kReqSetConfiguration    = 0x09;
constexpr std::uint8_t kReqGetInterface        = 0x0A;
constexpr std::uint8_t kReqSetInterface        = 0x0B;

} // namespace

Device& Device::instance()
{
    static Device device;
    return device;
}

void Device::init()
{
    sysreg::enable_usb_clock();
    endpoint_init();
}

void Device::endpoint_init()
{
    usbreg::ctrl()      = 0;

    // EP1 is the CDC notification endpoint and EP2 is the CDC data pair.
    usbreg::ep4_1_mod() = usbreg::kUep1TxEn;
    usbreg::ep2_3_mod() = usbreg::kUep2RxEn | usbreg::kUep2TxEn;

    usbreg::ep0_dma()   = static_cast<std::uint16_t>(reinterpret_cast<std::uintptr_t>(&m_ep0_buffer[0]));
    usbreg::ep1_dma()   = static_cast<std::uint16_t>(reinterpret_cast<std::uintptr_t>(&m_ep1_buffer[0]));
    usbreg::ep2_dma()   = static_cast<std::uint16_t>(reinterpret_cast<std::uintptr_t>(&m_ep2_buffer[0]));

    usbreg::ep0_ctrl()  = usbreg::kUepRResAck | usbreg::kUepTResNak;
    usbreg::ep1_ctrl()  = usbreg::kUepRResAck | usbreg::kUepTResNak;
    usbreg::ep2_ctrl()  = usbreg::kUepRResAck | usbreg::kUepTResNak;

    usbreg::dev_addr()  = 0;
    usbreg::ctrl()      = usbreg::kUcDevPuEn | usbreg::kUcIntBusy | usbreg::kUcDmaEn;
    sysreg::pin_analog_ie() |= sysreg::kPinUsbIe | sysreg::kPinUsbDpPu;
    usbreg::dev_ctrl() = usbreg::kUdPdDis;
    usbreg::int_fg()   = 0xFF;
    usbreg::int_en()   = usbreg::kUieSuspend | usbreg::kUieTransfer | usbreg::kUieBusReset;

    enable_irq(Irq::Usb);

    usbreg::dev_ctrl() |= usbreg::kUdPortEn;

    m_device_info = DeviceInfo{};
}

void Device::handle_setup()
{
    auto*        setup          = reinterpret_cast<SetupRequest*>(m_ep0_buffer);
    std::uint8_t data_direction = kRequestTypeIn;

    m_setup_length              = setup->length_l;
    if (setup->length_h != 0)
    {
        m_setup_length = 0xFF;
    }

    std::uint8_t length = 0;
    m_setup_request     = setup->request;
    if ((setup->request_type & kRequestTypeIn) == 0)
    {
        data_direction = 0;
    }

    const std::uint8_t request_type = setup->request_type & kRequestTypeMask;
    m_setup_type                    = request_type;

    if (request_type == kRequestTypeStandard)
    {
        switch (m_setup_request)
        {
        case kReqGetDescriptor:
        {
            const std::uint8_t  type              = setup->value_h;
            const std::uint8_t* descriptor        = find_descriptor(kDescriptors, type);
            std::uint16_t       descriptor_length = 0;
            if (descriptor != nullptr)
            {
                descriptor_length = descriptor[0];
                if (type == kDescriptorConfig && descriptor[0] >= 4)
                {
                    // For a configuration descriptor bLength is only the 9-byte
                    // header; the full size is wTotalLength.
                    descriptor_length = static_cast<std::uint16_t>(descriptor[2] | (descriptor[3] << 8));
                }
                if (descriptor_length > 255)
                {
                    descriptor_length = 255;
                }
            }

            if (descriptor == nullptr)
            {
                length = kStall;
                break;
            }

            m_descriptor = descriptor;
            length       = static_cast<std::uint8_t>(descriptor_length);
            if (m_setup_length > length)
            {
                m_setup_length = length;
            }
            length = m_setup_length >= kEp0Size ? kEp0Size : m_setup_length;
            std::memcpy(m_ep0_buffer, m_descriptor, length);
            m_setup_length = static_cast<std::uint8_t>(m_setup_length - length);
            m_descriptor += length;
            break;
        }

        case kReqSetAddress:
            m_device_info.address = setup->value_l;
            break;

        case kReqGetConfiguration:
            m_ep0_buffer[0] = m_device_info.config;
            if (m_setup_length >= 1)
            {
                length = 1;
            }
            break;

        case kReqSetConfiguration:
            m_device_info.config = setup->value_l;
            usbreg::ep1_ctrl()   = usbreg::kUepRResAck | usbreg::kUepTResNak;
            usbreg::ep2_ctrl()   = usbreg::kUepRResAck | usbreg::kUepTResNak;
            usbreg::ep1_t_len()  = 0;
            usbreg::ep2_t_len()  = 0;
            break;

        case kReqSetInterface:
            // CDC data interface 1 may be selected explicitly.
            if (setup->value_l == 1)
            {
                usbreg::ep2_ctrl()  = usbreg::kUepRResAck | usbreg::kUepTResNak;
                usbreg::ep2_t_len() = 0;
            }
            break;

        case kReqClearFeature:
            if ((setup->request_type & kRecipientMask) == kRecipientDevice)
            {
                usbreg::ep1_ctrl() =
                    (usbreg::ep1_ctrl() & ~(usbreg::kUepTTog | usbreg::kMaskUepTRes)) | usbreg::kUepTResNak;
                usbreg::ep2_ctrl() =
                    (usbreg::ep2_ctrl() & ~(usbreg::kUepTTog | usbreg::kMaskUepTRes)) | usbreg::kUepTResNak;
            }
            else if ((setup->request_type & kRecipientMask) == kRecipientEndpoint)
            {
                switch (setup->index_l)
                {
                case 0x82:
                    usbreg::ep2_ctrl() =
                        (usbreg::ep2_ctrl() & ~(usbreg::kUepTTog | usbreg::kMaskUepTRes)) | usbreg::kUepTResNak;
                    break;
                case 0x02:
                    usbreg::ep2_ctrl() =
                        (usbreg::ep2_ctrl() & ~(usbreg::kUepRTog | usbreg::kMaskUepRRes)) | usbreg::kUepRResAck;
                    break;
                case 0x81:
                    usbreg::ep1_ctrl() =
                        (usbreg::ep1_ctrl() & ~(usbreg::kUepTTog | usbreg::kMaskUepTRes)) | usbreg::kUepTResNak;
                    break;
                default:
                    length = kStall;
                    break;
                }
            }
            else
            {
                length = kStall;
            }
            break;

        case kReqGetInterface:
            m_ep0_buffer[0] = 0;
            if (m_setup_length >= 1)
            {
                length = 1;
            }
            break;

        case kReqGetStatus:
            m_ep0_buffer[0] = 0;
            m_ep0_buffer[1] = 0;
            length          = m_setup_length >= 2 ? 2 : m_setup_length;
            break;

        default:
            length = kStall;
            break;
        }
    }
    else if (request_type == kRequestTypeClass)
    {
        if (data_direction != 0 && m_setup_request == kCdcGetLineCoding)
        {
            length = static_cast<std::uint8_t>(kCdcLineCodingSize);
            std::memcpy(m_ep0_buffer, &m_line_coding, kCdcLineCodingSize);
        }
        else if (data_direction != 0 && m_setup_request == kCdcGetCommFeature)
        {
            length          = m_setup_length >= 2 ? 2 : m_setup_length;
            m_ep0_buffer[0] = 0;
            m_ep0_buffer[1] = 0;
        }
        else if (m_setup_request == kCdcSetControlLineState)
        {
            m_control_state     = setup->value_l;

            // SERIAL_STATE: DCD | DSR, on the CDC notification endpoint.
            m_ep1_buffer[0]     = 0xA1;
            m_ep1_buffer[1]     = 0x20;
            m_ep1_buffer[2]     = 0x00;
            m_ep1_buffer[3]     = 0x00;
            m_ep1_buffer[4]     = 0x00;
            m_ep1_buffer[5]     = 0x00;
            m_ep1_buffer[6]     = 0x02;
            m_ep1_buffer[7]     = 0x00;
            m_ep1_buffer[8]     = 0x03;
            m_ep1_buffer[9]     = 0x00;
            usbreg::ep1_t_len() = 10;
            usbreg::ep1_ctrl()  = (usbreg::ep1_ctrl() & ~usbreg::kMaskUepTRes) | usbreg::kUepTResAck;

            length              = 0;
        }
        else if (m_setup_request == kCdcSendBreak)
        {
            m_break_pending = true;
            length          = 0;
        }
        else
        {
            // SET_LINE_CODING and other class requests carry no IN payload here.
            length = 0;
        }
    }
    else
    {
        length = kStall;
    }

    if (length == kStall)
    {
        m_setup_request    = kStall;
        usbreg::ep0_ctrl() = usbreg::kUepRTog | usbreg::kUepTTog | usbreg::kUepRResStall | usbreg::kUepTResStall;
        return;
    }

    const bool data_in  = (length <= kEp0Size);
    const bool out_data = (data_direction == 0 && m_setup_length != 0);
    usbreg::ep0_t_len() = data_in ? length : 0;
    const std::uint8_t rx_response =
        out_data ? usbreg::kUepRResAck
                 : ((data_in && m_setup_request != kReqGetDescriptor) ? usbreg::kUepRResNak : usbreg::kUepRResAck);
    usbreg::ep0_ctrl() = usbreg::kUepRTog | usbreg::kUepTTog | rx_response | usbreg::kUepTResAck;
}

void Device::irq_handler()
{
    if ((usbreg::int_fg() & usbreg::kUifTransfer) != 0)
    {
        if ((usbreg::int_st() & usbreg::kMaskUisToken) != usbreg::kMaskUisToken)
        {
            const std::uint8_t pid       = usbreg::int_st();
            const std::uint8_t length    = usbreg::rx_len();
            const bool         toggle_ok = (usbreg::int_fg() & kToggleOk) != 0;

            switch (pid & 0x3Fu)
            {
            case kTokenOut | 2:
                if (toggle_ok)
                {
                    usbreg::ep2_ctrl() ^= usbreg::kUepRTog;
                    m_rx_buffer.write(m_ep2_buffer, length);

                    if (m_rx_buffer.size() > (kUsbBufferSize - kPacketSize))
                    {
                        usbreg::ep2_ctrl() = (usbreg::ep2_ctrl() & ~usbreg::kMaskUepRRes) | usbreg::kUepRResNak;
                    }
                }
                break;

            case kTokenIn | 2:
                usbreg::ep2_ctrl() ^= usbreg::kUepTTog;
                usbreg::ep2_ctrl() = (usbreg::ep2_ctrl() & ~usbreg::kMaskUepTRes) | kInNak;
                break;

            case kTokenIn | 1:
                usbreg::ep1_ctrl() ^= usbreg::kUepTTog;
                usbreg::ep1_ctrl() = (usbreg::ep1_ctrl() & 0xFCu) | kInNak;
                break;

            case kTokenOut | 0:
                // SET_LINE_CODING carries its seven bytes in the OUT data
                // stage. The SIE reports the packet even while the data stage
                // is NAKed, so take it once EP0 has received it.
                if (m_setup_type == kRequestTypeClass && m_setup_request == kCdcSetLineCoding
                    && static_cast<std::size_t>(length) == sizeof(serial::LineCoding))
                {
                    std::memcpy(&m_line_coding, m_ep0_buffer, sizeof(serial::LineCoding));
                    ++m_line_coding_generation;
                }

                if (toggle_ok)
                {
                    usbreg::ep0_ctrl() =
                        usbreg::kUepRTog | usbreg::kUepTTog | usbreg::kUepRResNak | usbreg::kUepTResAck;
                }
                break;

            case kTokenIn | 0:
                usbreg::ep0_ctrl() = (usbreg::ep0_ctrl() & 0xFCu) | kInNak;
                handle_ep0_in();
                break;

            default:
                break;
            }

            usbreg::int_fg() = usbreg::kUifTransfer;
        }

        if ((usbreg::int_st() & usbreg::kUisSetupAct) != 0)
        {
            handle_setup();
            usbreg::int_fg() = usbreg::kUifTransfer;
        }
    }
    else if ((usbreg::int_fg() & usbreg::kUifBusReset) != 0)
    {
        usbreg::ep0_ctrl()    = usbreg::kUepRResAck | usbreg::kUepTResNak;
        usbreg::ep1_ctrl()    = usbreg::kUepRResAck | usbreg::kUepTResNak;
        usbreg::ep2_ctrl()    = usbreg::kUepRResAck | usbreg::kUepTResNak;
        usbreg::dev_addr()    = 0;
        m_device_info.address = 0;
        m_device_info.config  = 0;
        usbreg::int_fg()      = usbreg::kUifBusReset;
    }
    else if ((usbreg::int_fg() & usbreg::kUifSuspend) != 0)
    {
        usbreg::int_fg() = usbreg::kUifSuspend;
    }
}

void Device::handle_ep0_in()
{
    if (m_setup_type == kRequestTypeClass
        && (m_setup_request == kCdcGetLineCoding || m_setup_request == kCdcGetCommFeature))
    {
        usbreg::ep0_ctrl() = usbreg::kUepRTog | usbreg::kUepTTog | usbreg::kUepRResAck | usbreg::kUepTResNak;
        return;
    }

    switch (m_setup_request)
    {
    case kReqGetDescriptor:
    {
        std::uint8_t length = m_setup_length >= kEp0Size ? kEp0Size : m_setup_length;
        std::memcpy(m_ep0_buffer, m_descriptor, length);
        m_setup_length = static_cast<std::uint8_t>(m_setup_length - length);
        m_descriptor += length;

        if (length != 0)
        {
            usbreg::ep0_t_len() = length;
            usbreg::ep0_ctrl() ^= usbreg::kUepTTog;
            usbreg::ep0_ctrl() &= ~0x03u;
        }
        else
        {
            usbreg::ep0_t_len() = length;
            usbreg::ep0_ctrl()  = usbreg::kUepRTog | usbreg::kUepTTog | usbreg::kUepRResAck | usbreg::kUepTResNak;
        }
        break;
    }

    case kReqSetAddress:
        usbreg::dev_addr() = (usbreg::dev_addr() & 0x80u) | m_device_info.address;
        usbreg::ep0_ctrl() = usbreg::kUepRTog | usbreg::kUepTTog | usbreg::kUepRResNak | usbreg::kUepTResNak;
        break;

    default:
        usbreg::ep0_t_len() = 0;
        usbreg::ep0_ctrl()  = usbreg::kUepRTog | usbreg::kUepTTog | usbreg::kUepRResNak | usbreg::kUepTResNak;
        break;
    }
}

void Device::poll()
{
    if (!enumerated())
    {
        return;
    }

    if ((usbreg::ep2_ctrl() & usbreg::kMaskUepTRes) == usbreg::kUepTResNak)
    {
        if (m_tx != nullptr)
        {
            std::uint8_t length = static_cast<std::uint8_t>(std::min<std::size_t>(kPacketSize, m_tx->size()));
            if (length != 0)
            {
                length              = static_cast<std::uint8_t>(m_tx->read(&m_ep2_buffer[kPacketSize], length));
                usbreg::ep2_t_len() = length;
                usbreg::ep2_ctrl()  = (usbreg::ep2_ctrl() & ~usbreg::kMaskUepTRes) | usbreg::kUepTResAck;
            }
        }
    }
}

std::size_t Device::read_host(void* dst, std::size_t max_length) noexcept
{
    if (m_rx_buffer.empty())
    {
        return 0;
    }

    const std::size_t length = m_rx_buffer.read(dst, max_length);
    if (m_rx_buffer.size() < (kUsbBufferSize - kPacketSize))
    {
        usbreg::ep2_ctrl() = (usbreg::ep2_ctrl() & ~usbreg::kMaskUepRRes) | usbreg::kUepRResAck;
    }
    return length;
}

} // namespace ch582::usb

extern "C"
{
CH582_INTERRUPT CH582_HIGH_CODE void USB_IRQHandler(void)
{
    ch582::usb::Device::instance().irq_handler();
}

CH582_INTERRUPT CH582_HIGH_CODE void USB2_IRQHandler(void)
{
    // Host transactions are polled synchronously; USB2 interrupts stay off.
}
}
