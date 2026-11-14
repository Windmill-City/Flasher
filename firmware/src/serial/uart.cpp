#include "ch582/serial/uart.hpp"

#include "ch582/platform/clock.hpp"
#include "ch582/platform/registers.hpp"

namespace ch582::serial
{

Uart* g_uart1_instance = nullptr;

namespace
{

inline constexpr std::size_t   kTxFifoDepth = 8;

std::uint8_t make_lcr(const LineCoding& coding) noexcept
{
    std::uint8_t data_bits = coding.data_bits;
    if (data_bits < 5 || data_bits > 8)
    {
        data_bits = 8;
    }

    std::uint8_t lcr = static_cast<std::uint8_t>(data_bits - 5);
    if (coding.stop_bits == kStopBits2)
    {
        lcr |= uart1::kLcrStopBits;
    }

    switch (coding.parity)
    {
    case kParityOdd:
        lcr |= uart1::kLcrParityEnable;
        break;

    case kParityEven:
        lcr |= uart1::kLcrParityEnable | 0x10u;
        break;

    case kParityMark:
        lcr |= uart1::kLcrParityEnable | 0x20u;
        break;

    case kParitySpace:
        lcr |= uart1::kLcrParityEnable | 0x30u;
        break;

    default:
        break;
    }

    return lcr;
}

} // namespace

void Uart::set_baud(std::uint32_t baud) noexcept
{
    if (baud < 1200u)
    {
        baud = 1200u;
    }

    std::uint32_t divisor = ((10u * clock::kSysClockHz / 8u / baud) + 5u) / 10u;
    if (divisor == 0)
    {
        divisor = 1;
    }
    if (divisor > 0xFFFFu)
    {
        divisor = 0xFFFFu;
    }

    uart1::divisor() = static_cast<std::uint16_t>(divisor);
    uart1::div()     = 1;
}

void Uart::init(std::uint32_t baud) noexcept
{
    g_uart1_instance = this;
    // PB12/PB13 belong to USB2 when the host is active; take them back as
    // digital UART pins.
    sysreg::pin_analog_ie() &= static_cast<std::uint16_t>(~(sysreg::kPinUsb2Ie | sysreg::kPinUsb2DpPu));
    sysreg::pin_alternate() |= sysreg::kPinUart1;

    gpio::pb::pd_drv() &= ~(gpio::kPin12 | gpio::kPin13);
    gpio::pb::pu() |= (gpio::kPin12 | gpio::kPin13);
    gpio::pb::out() |= (gpio::kPin12 | gpio::kPin13);
    gpio::pb::dir() |= gpio::kPin13;
    gpio::pb::dir() &= ~gpio::kPin12;

    sysreg::enable_uart1_clock();

    set_baud(baud);

    uart1::fcr() = static_cast<std::uint8_t>((2u << 6) | uart1::kFcrFifoEnable | uart1::kFcrRxClear
                                            | uart1::kFcrTxClear);
    uart1::lcr() = uart1::kLcrWordSizeMask;
    uart1::ier() = uart1::kIerTxdEnable | uart1::kIerRecvReady;
    uart1::mcr() |= uart1::kMcrIrqOutput;

    m_rx.clear();
    m_active = true;
    enable_irq(Irq::Uart1);
}

void Uart::deinit() noexcept
{
    m_active = false;
    disable_irq(Irq::Uart1);

    uart1::ier() = 0;
    uart1::mcr() = 0;
    sysreg::pin_alternate() &= static_cast<std::uint16_t>(~sysreg::kPinUart1);

    gpio::pb::dir() &= ~(gpio::kPin12 | gpio::kPin13);
    gpio::pb::pu() &= ~(gpio::kPin12 | gpio::kPin13);
    gpio::pb::out() &= ~(gpio::kPin12 | gpio::kPin13);
}

void Uart::configure(const LineCoding& coding) noexcept
{
    if (!m_active)
    {
        return;
    }

    CriticalSection guard;

    set_baud(coding.baud_rate);
    uart1::lcr() = make_lcr(coding);

    std::uint8_t scratch[32];
    while (m_rx.read(scratch, sizeof(scratch)) != 0)
    {
    }
}

CH582_HIGH_CODE std::size_t Uart::read(std::uint8_t* dst, std::size_t max_length) noexcept
{
    return m_rx.read(dst, max_length);
}

void Uart::write(const std::uint8_t* src, std::size_t length) noexcept
{
    if (src == nullptr)
    {
        return;
    }

    for (std::size_t index = 0; index < length; ++index)
    {
        while (uart1::tfc() >= kTxFifoDepth)
        {
        }
        uart1::thr() = src[index];
    }
}

CH582_HIGH_CODE void Uart::irq_handler() noexcept
{
    std::uint8_t      buffer[kTxFifoDepth];
    const std::size_t count = uart1::rfc();
    std::size_t       index = 0;

    while (index < count && index < sizeof(buffer))
    {
        buffer[index++] = uart1::rbr();
    }

    m_rx.write(buffer, index);
}

} // namespace ch582::serial

extern "C"
{
CH582_INTERRUPT CH582_HIGH_CODE void UART1_IRQHandler(void)
{
    if (ch582::serial::g_uart1_instance != nullptr)
    {
        ch582::serial::g_uart1_instance->irq_handler();
    }
}
}
