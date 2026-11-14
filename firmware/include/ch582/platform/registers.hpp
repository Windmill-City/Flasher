#pragma once

#include <cstddef>
#include <cstdint>

// CH582F register subset used by the Flasher firmware.
//
// The addresses and bit fields are taken from CH583SFR.h (WCH CH582/CH583 SDK)
// and the CH583/CH582 datasheet. CH582 has two independent USB controllers:
//   USB  (UD+/UD-)   at 0x40008000  -> Type-C, PC side, device mode
//   USB2 (U2D+/U2D-) at 0x40008400  -> USB-A, Dongle side, host/UART mode

namespace ch582
{

namespace mmio
{

template <typename T>
inline volatile T& ref(std::uintptr_t address) noexcept
{
    return *reinterpret_cast<volatile T*>(address);
}

} // namespace mmio

inline void fence_i() noexcept
{
    asm volatile("fence.i" ::: "memory");
}

inline void nop() noexcept
{
    asm volatile("nop");
}

inline std::uint32_t irq_save() noexcept
{
    std::uint32_t state;
    asm volatile("csrrc %0, 0x800, %1" : "=r"(state) : "r"(0x88u) : "memory");
    return state & 0x88u;
}

inline void irq_restore(std::uint32_t state) noexcept
{
    asm volatile("csrw 0x800, %0" : : "r"(state) : "memory");
}

inline void enable_global_irq() noexcept
{
    asm volatile("csrw 0x800, %0" : : "r"(0x88u) : "memory");
}

inline void disable_global_irq() noexcept
{
    asm volatile("csrw 0x800, %0" : : "r"(0x80u) : "memory");
}

class CriticalSection
{
  public:
    CriticalSection() noexcept
        : m_state(irq_save())
    {
    }

    ~CriticalSection() noexcept
    {
        irq_restore(m_state);
    }

    CriticalSection(const CriticalSection&)            = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;

  private:
    std::uint32_t m_state;
};

// ---------------------------------------------------------------------------
// PFIC
// ---------------------------------------------------------------------------

enum class Irq : std::uint32_t
{
    Reset        = 0,
    Nmi          = 2,
    Exception    = 3,
    EcallMachine = 5,
    EcallUser    = 8,
    Breakpoint   = 9,
    SystemTick   = 12,
    Software     = 14,
    Tmr0         = 16,
    GpioA        = 17,
    GpioB        = 18,
    Spi0         = 19,
    BleLle       = 20,
    BleBb        = 21,
    Usb          = 22,
    Usb2         = 23,
    Tmr1         = 24,
    Tmr2         = 25,
    Uart0        = 26,
    Uart1        = 27,
    Rtc          = 28,
    Adc          = 29,
    I2c          = 30,
    PwmxSpi1     = 31,
    Tmr3         = 32,
    Uart2        = 33,
    Uart3        = 34,
    WatchdogBat  = 35,
};

struct Pfic
{
    volatile std::uint32_t isr[8];
    volatile std::uint32_t ipr[8];
    volatile std::uint32_t ithresdr;
    std::uint32_t          reserved0;
    volatile std::uint32_t cfgr;
    volatile std::uint32_t gisr;
    volatile std::uint8_t  vtfidr[4];
    std::uint8_t           reserved1[0x0C];
    volatile std::uint32_t vtfaddr[4];
    std::uint8_t           reserved2[0x90];
    volatile std::uint32_t ienr[8];
    std::uint8_t           reserved3[0x60];
    volatile std::uint32_t irer[8];
    std::uint8_t           reserved4[0x60];
    volatile std::uint32_t ipsr[8];
    std::uint8_t           reserved5[0x60];
    volatile std::uint32_t iprr[8];
    std::uint8_t           reserved6[0x60];
    volatile std::uint32_t iactr[8];
    std::uint8_t           reserved7[0xE0];
    volatile std::uint8_t  iprior[256];
    std::uint8_t           reserved8[0x810];
    volatile std::uint32_t sctlr;
};

static_assert(offsetof(Pfic, cfgr) == 0x48, "PFIC CFGR offset");
static_assert(offsetof(Pfic, ienr) == 0x100, "PFIC IENR offset");
static_assert(offsetof(Pfic, iprior) == 0x400, "PFIC IPRIOR offset");

inline Pfic& pfic() noexcept
{
    return *reinterpret_cast<Pfic*>(0xE000E000u);
}

inline void enable_irq(Irq irq) noexcept
{
    const auto index   = static_cast<std::uint32_t>(irq) >> 5;
    const auto bit     = 1u << (static_cast<std::uint32_t>(irq) & 0x1Fu);
    pfic().ienr[index] = bit;
}

inline void disable_irq(Irq irq) noexcept
{
    const auto index   = static_cast<std::uint32_t>(irq) >> 5;
    const auto bit     = 1u << (static_cast<std::uint32_t>(irq) & 0x1Fu);
    pfic().irer[index] = bit;
    fence_i();
}

// ---------------------------------------------------------------------------
// Core SysTick (free running, no interrupt)
// ---------------------------------------------------------------------------

namespace systick
{

inline constexpr std::uintptr_t kBase = 0xE000F000u;

inline volatile std::uint32_t&  ctrl() noexcept
{
    return mmio::ref<std::uint32_t>(kBase + 0x00u);
}

inline volatile std::uint32_t& now() noexcept
{
    return mmio::ref<std::uint32_t>(kBase + 0x08u);
}

inline constexpr std::uint32_t kCtrlEnable    = 1u << 0;
inline constexpr std::uint32_t kCtrlClockHclk = 1u << 2;

inline bool                    expired(std::uint32_t deadline) noexcept
{
    return static_cast<std::int32_t>(now() - deadline) >= 0;
}

inline void delay(std::uint32_t ticks) noexcept
{
    const std::uint32_t deadline = now() + ticks;
    while (static_cast<std::int32_t>(now() - deadline) < 0)
        ;
}

} // namespace systick

// ---------------------------------------------------------------------------
// SYS
// ---------------------------------------------------------------------------

namespace sysreg
{

inline constexpr std::uint8_t kSafeAccessSig0 = 0x00;
inline constexpr std::uint8_t kSafeAccessSig1 = 0x57;
inline constexpr std::uint8_t kSafeAccessSig2 = 0xA8;

inline volatile std::uint8_t& safe_access_sig() noexcept
{
    return mmio::ref<std::uint8_t>(0x40001040u);
}

inline volatile std::uint8_t& reset_watchdog_ctrl() noexcept
{
    return mmio::ref<std::uint8_t>(0x40001046u);
}

inline constexpr std::uint8_t  kWatchdogResetEnable = 0x02;
inline constexpr std::uint8_t  kWatchdogIrqEnable   = 0x04;
inline constexpr std::uint8_t  kSoftwareReset       = 0x01;
inline constexpr std::uint8_t  kBootLoadManual      = 0x00;

inline volatile std::uint16_t& clock_sys_cfg() noexcept
{
    return mmio::ref<std::uint16_t>(0x40001008u);
}

inline volatile std::uint8_t& hfck_power_ctrl() noexcept
{
    return mmio::ref<std::uint8_t>(0x4000100Au);
}

inline constexpr std::uint8_t kClockPllOn     = 0x10;
inline constexpr std::uint8_t kClockXt32mKeep = 0x08;
inline constexpr std::uint8_t kClockXt32mOn   = 0x04;

inline volatile std::uint8_t& sleep_clk_off0() noexcept
{
    return mmio::ref<std::uint8_t>(0x4000100Cu);
}

inline volatile std::uint8_t& sleep_clk_off1() noexcept
{
    return mmio::ref<std::uint8_t>(0x4000100Du);
}

inline volatile std::uint8_t& sleep_wake_ctrl() noexcept
{
    return mmio::ref<std::uint8_t>(0x4000100Eu);
}

inline volatile std::uint8_t& sleep_power_ctrl() noexcept
{
    return mmio::ref<std::uint8_t>(0x4000100Fu);
}

inline constexpr std::uint8_t  kSleepClkUart1 = 0x20;
inline constexpr std::uint8_t  kSleepClkUsb   = 0x10;
inline constexpr std::uint8_t  kSleepClkUsb2  = 0x20;

inline volatile std::uint16_t& pin_alternate() noexcept
{
    return mmio::ref<std::uint16_t>(0x40001018u);
}

inline volatile std::uint16_t& pin_analog_ie() noexcept
{
    return mmio::ref<std::uint16_t>(0x4000101Au);
}

inline constexpr std::uint16_t kPinUart1    = 0x0020;
inline constexpr std::uint16_t kPinUsb2Ie   = 0x0020;
inline constexpr std::uint16_t kPinUsb2DpPu = 0x0010;
inline constexpr std::uint16_t kPinUsbIe    = 0x0080;
inline constexpr std::uint16_t kPinUsbDpPu  = 0x0040;

inline volatile std::uint32_t& misc_ctrl() noexcept
{
    return mmio::ref<std::uint32_t>(0x40001048u);
}

inline volatile std::uint8_t& pll_config() noexcept
{
    return mmio::ref<std::uint8_t>(0x4000104Bu);
}

inline volatile std::uint8_t& xt32m_tune() noexcept
{
    return mmio::ref<std::uint8_t>(0x4000104Eu);
}

inline volatile std::uint8_t& flash_cfg() noexcept
{
    return mmio::ref<std::uint8_t>(0x40001807u);
}

class SafeAccess
{
  public:
    SafeAccess() noexcept
        : m_state(irq_save())
    {
        fence_i();
        safe_access_sig() = kSafeAccessSig1;
        safe_access_sig() = kSafeAccessSig2;
        fence_i();
    }

    ~SafeAccess() noexcept
    {
        safe_access_sig() = kSafeAccessSig0;
        irq_restore(m_state);
        fence_i();
    }

    SafeAccess(const SafeAccess&)            = delete;
    SafeAccess& operator=(const SafeAccess&) = delete;

  private:
    std::uint32_t m_state;
};

inline void disable_watchdog_reset() noexcept
{
    SafeAccess access;
    reset_watchdog_ctrl() &= static_cast<std::uint8_t>(~(kWatchdogResetEnable | kWatchdogIrqEnable));
}

inline void enable_usb_clock() noexcept
{
    SafeAccess access;
    sleep_clk_off1() &= static_cast<std::uint8_t>(~(kSleepClkUsb | kSleepClkUsb2));
}

inline void enable_uart1_clock() noexcept
{
    SafeAccess access;
    sleep_clk_off0() &= static_cast<std::uint8_t>(~kSleepClkUart1);
}

} // namespace sysreg

// ---------------------------------------------------------------------------
// GPIO
// ---------------------------------------------------------------------------

namespace gpio
{

inline constexpr std::uint32_t kPin0  = 1u << 0;
inline constexpr std::uint32_t kPin1  = 1u << 1;
inline constexpr std::uint32_t kPin2  = 1u << 2;
inline constexpr std::uint32_t kPin3  = 1u << 3;
inline constexpr std::uint32_t kPin4  = 1u << 4;
inline constexpr std::uint32_t kPin5  = 1u << 5;
inline constexpr std::uint32_t kPin6  = 1u << 6;
inline constexpr std::uint32_t kPin7  = 1u << 7;
inline constexpr std::uint32_t kPin8  = 1u << 8;
inline constexpr std::uint32_t kPin9  = 1u << 9;
inline constexpr std::uint32_t kPin10 = 1u << 10;
inline constexpr std::uint32_t kPin11 = 1u << 11;
inline constexpr std::uint32_t kPin12 = 1u << 12;
inline constexpr std::uint32_t kPin13 = 1u << 13;
inline constexpr std::uint32_t kPin14 = 1u << 14;
inline constexpr std::uint32_t kPin15 = 1u << 15;
inline constexpr std::uint32_t kPin22 = 1u << 22;
inline constexpr std::uint32_t kPin23 = 1u << 23;

namespace pa
{
inline volatile std::uint32_t& dir() noexcept
{
    return mmio::ref<std::uint32_t>(0x400010A0u);
}

inline volatile std::uint32_t& pin() noexcept
{
    return mmio::ref<std::uint32_t>(0x400010A4u);
}

inline volatile std::uint32_t& out() noexcept
{
    return mmio::ref<std::uint32_t>(0x400010A8u);
}

inline volatile std::uint32_t& clr() noexcept
{
    return mmio::ref<std::uint32_t>(0x400010ACu);
}

inline volatile std::uint32_t& pu() noexcept
{
    return mmio::ref<std::uint32_t>(0x400010B0u);
}

inline volatile std::uint32_t& pd_drv() noexcept
{
    return mmio::ref<std::uint32_t>(0x400010B4u);
}
} // namespace pa

namespace pb
{
inline volatile std::uint32_t& dir() noexcept
{
    return mmio::ref<std::uint32_t>(0x400010C0u);
}

inline volatile std::uint32_t& pin() noexcept
{
    return mmio::ref<std::uint32_t>(0x400010C4u);
}

inline volatile std::uint32_t& out() noexcept
{
    return mmio::ref<std::uint32_t>(0x400010C8u);
}

inline volatile std::uint32_t& clr() noexcept
{
    return mmio::ref<std::uint32_t>(0x400010CCu);
}

inline volatile std::uint32_t& pu() noexcept
{
    return mmio::ref<std::uint32_t>(0x400010D0u);
}

inline volatile std::uint32_t& pd_drv() noexcept
{
    return mmio::ref<std::uint32_t>(0x400010D4u);
}
} // namespace pb

} // namespace gpio

// ---------------------------------------------------------------------------
// UART1 (PB12 = RXD, PB13 = TXD when remapped)
// ---------------------------------------------------------------------------

namespace uart1
{

inline volatile std::uint8_t& mcr() noexcept
{
    return mmio::ref<std::uint8_t>(0x40003400u);
}

inline volatile std::uint8_t& ier() noexcept
{
    return mmio::ref<std::uint8_t>(0x40003401u);
}

inline volatile std::uint8_t& fcr() noexcept
{
    return mmio::ref<std::uint8_t>(0x40003402u);
}

inline volatile std::uint8_t& lcr() noexcept
{
    return mmio::ref<std::uint8_t>(0x40003403u);
}

inline volatile std::uint8_t& lsr() noexcept
{
    return mmio::ref<std::uint8_t>(0x40003405u);
}

inline volatile std::uint8_t& rbr() noexcept
{
    return mmio::ref<std::uint8_t>(0x40003408u);
}

inline volatile std::uint8_t& thr() noexcept
{
    return mmio::ref<std::uint8_t>(0x40003408u);
}

inline volatile std::uint8_t& rfc() noexcept
{
    return mmio::ref<std::uint8_t>(0x4000340Au);
}

inline volatile std::uint8_t& tfc() noexcept
{
    return mmio::ref<std::uint8_t>(0x4000340Bu);
}

inline volatile std::uint16_t& divisor() noexcept
{
    return mmio::ref<std::uint16_t>(0x4000340Cu);
}

inline volatile std::uint8_t& div() noexcept
{
    return mmio::ref<std::uint8_t>(0x4000340Eu);
}

inline constexpr std::uint8_t kMcrIrqOutput    = 0x08;
inline constexpr std::uint8_t kIerRecvReady    = 0x01;
inline constexpr std::uint8_t kIerTxdEnable    = 0x40;
inline constexpr std::uint8_t kFcrFifoEnable   = 0x01;
inline constexpr std::uint8_t kFcrRxClear      = 0x02;
inline constexpr std::uint8_t kFcrTxClear      = 0x04;
inline constexpr std::uint8_t kLcrWordSizeMask = 0x03;
inline constexpr std::uint8_t kLcrStopBits     = 0x04;
inline constexpr std::uint8_t kLcrParityEnable = 0x08;

} // namespace uart1

// ---------------------------------------------------------------------------
// USB controllers
// ---------------------------------------------------------------------------

namespace usbreg
{

inline constexpr std::uintptr_t kBase = 0x40008000u;

template <typename T>
inline volatile T& at(std::size_t offset) noexcept
{
    return mmio::ref<T>(kBase + offset);
}

inline volatile std::uint8_t& ctrl() noexcept
{
    return at<std::uint8_t>(0x00);
}

inline volatile std::uint8_t& dev_ctrl() noexcept
{
    return at<std::uint8_t>(0x01);
}

inline volatile std::uint8_t& int_en() noexcept
{
    return at<std::uint8_t>(0x02);
}

inline volatile std::uint8_t& dev_addr() noexcept
{
    return at<std::uint8_t>(0x03);
}

inline volatile std::uint8_t& misc_status() noexcept
{
    return at<std::uint8_t>(0x05);
}

inline volatile std::uint8_t& int_fg() noexcept
{
    return at<std::uint8_t>(0x06);
}

inline volatile std::uint8_t& int_st() noexcept
{
    return at<std::uint8_t>(0x07);
}

inline volatile std::uint8_t& rx_len() noexcept
{
    return at<std::uint8_t>(0x08);
}

inline volatile std::uint8_t& ep4_1_mod() noexcept
{
    return at<std::uint8_t>(0x0C);
}

inline volatile std::uint8_t& ep2_3_mod() noexcept
{
    return at<std::uint8_t>(0x0D);
}

inline volatile std::uint16_t& ep0_dma() noexcept
{
    return at<std::uint16_t>(0x10);
}

inline volatile std::uint16_t& ep1_dma() noexcept
{
    return at<std::uint16_t>(0x14);
}

inline volatile std::uint16_t& ep2_dma() noexcept
{
    return at<std::uint16_t>(0x18);
}

inline volatile std::uint8_t& ep0_t_len() noexcept
{
    return at<std::uint8_t>(0x20);
}

inline volatile std::uint8_t& ep0_ctrl() noexcept
{
    return at<std::uint8_t>(0x22);
}

inline volatile std::uint8_t& ep1_t_len() noexcept
{
    return at<std::uint8_t>(0x24);
}

inline volatile std::uint8_t& ep1_ctrl() noexcept
{
    return at<std::uint8_t>(0x26);
}

inline volatile std::uint8_t& ep2_t_len() noexcept
{
    return at<std::uint8_t>(0x28);
}

inline volatile std::uint8_t& ep2_ctrl() noexcept
{
    return at<std::uint8_t>(0x2A);
}

inline constexpr std::uint8_t kUcHostMode     = 0x80;
inline constexpr std::uint8_t kUcLowSpeed     = 0x40;
inline constexpr std::uint8_t kUcDevPuEn      = 0x20;
inline constexpr std::uint8_t kUcIntBusy      = 0x08;
inline constexpr std::uint8_t kUcResetSie     = 0x04;
inline constexpr std::uint8_t kUcClrAll       = 0x02;
inline constexpr std::uint8_t kUcDmaEn        = 0x01;

inline constexpr std::uint8_t kUdPdDis        = 0x80;
inline constexpr std::uint8_t kUdPortEn       = 0x01;

inline constexpr std::uint8_t kUieSuspend     = 0x04;
inline constexpr std::uint8_t kUieTransfer    = 0x02;
inline constexpr std::uint8_t kUieBusReset    = 0x01;

inline constexpr std::uint8_t kUifToggleOk    = 0x40;
inline constexpr std::uint8_t kUifSuspend     = 0x04;
inline constexpr std::uint8_t kUifTransfer    = 0x02;
inline constexpr std::uint8_t kUifBusReset    = 0x01;

inline constexpr std::uint8_t kUisSetupAct    = 0x80;
inline constexpr std::uint8_t kUisToggleOk    = 0x40;
inline constexpr std::uint8_t kMaskUisToken   = 0x30;
inline constexpr std::uint8_t kUisTokenOut    = 0x00;
inline constexpr std::uint8_t kUisTokenIn     = 0x20;

inline constexpr std::uint8_t kUep1RxEn       = 0x80;
inline constexpr std::uint8_t kUep1TxEn       = 0x40;
inline constexpr std::uint8_t kUep2RxEn       = 0x08;
inline constexpr std::uint8_t kUep2TxEn       = 0x04;

inline constexpr std::uint8_t kUepRTog        = 0x80;
inline constexpr std::uint8_t kUepTTog        = 0x40;
inline constexpr std::uint8_t kUepAutoTog     = 0x10;
inline constexpr std::uint8_t kMaskUepRRes    = 0x0C;
inline constexpr std::uint8_t kUepRResAck     = 0x00;
inline constexpr std::uint8_t kUepRResTimeout = 0x04;
inline constexpr std::uint8_t kUepRResNak     = 0x08;
inline constexpr std::uint8_t kUepRResStall   = 0x0C;
inline constexpr std::uint8_t kMaskUepTRes    = 0x03;
inline constexpr std::uint8_t kUepTResAck     = 0x00;
inline constexpr std::uint8_t kUepTResTimeout = 0x01;
inline constexpr std::uint8_t kUepTResNak     = 0x02;
inline constexpr std::uint8_t kUepTResStall   = 0x03;

} // namespace usbreg

namespace usb2reg
{

inline constexpr std::uintptr_t kBase = 0x40008400u;

template <typename T>
inline volatile T& at(std::size_t offset) noexcept
{
    return mmio::ref<T>(kBase + offset);
}

inline volatile std::uint8_t& ctrl() noexcept
{
    return at<std::uint8_t>(0x00);
}

inline volatile std::uint8_t& host_ctrl() noexcept
{
    return at<std::uint8_t>(0x01);
}

inline volatile std::uint8_t& int_en() noexcept
{
    return at<std::uint8_t>(0x02);
}

inline volatile std::uint8_t& dev_addr() noexcept
{
    return at<std::uint8_t>(0x03);
}

inline volatile std::uint8_t& misc_status() noexcept
{
    return at<std::uint8_t>(0x05);
}

inline volatile std::uint8_t& int_fg() noexcept
{
    return at<std::uint8_t>(0x06);
}

inline volatile std::uint8_t& int_st() noexcept
{
    return at<std::uint8_t>(0x07);
}

inline volatile std::uint8_t& rx_len() noexcept
{
    return at<std::uint8_t>(0x08);
}

inline volatile std::uint8_t& ep2_3_mod() noexcept
{
    return at<std::uint8_t>(0x0D);
}

inline volatile std::uint16_t& ep2_dma() noexcept
{
    return at<std::uint16_t>(0x18);
}

inline volatile std::uint16_t& ep3_dma() noexcept
{
    return at<std::uint16_t>(0x1C);
}

inline volatile std::uint8_t& ep1_ctrl() noexcept
{
    return at<std::uint8_t>(0x26);
}

inline volatile std::uint8_t& ep2_t_len() noexcept
{
    return at<std::uint8_t>(0x28);
}

inline volatile std::uint8_t& ep2_ctrl() noexcept
{
    return at<std::uint8_t>(0x2A);
}

inline volatile std::uint8_t& ep3_t_len() noexcept
{
    return at<std::uint8_t>(0x2C);
}

inline volatile std::uint8_t& ep3_ctrl() noexcept
{
    return at<std::uint8_t>(0x2E);
}

inline volatile std::uint8_t& host_ep_mod() noexcept
{
    return ep2_3_mod();
}

inline volatile std::uint16_t& host_rx_dma() noexcept
{
    return ep2_dma();
}

inline volatile std::uint16_t& host_tx_dma() noexcept
{
    return ep3_dma();
}

inline volatile std::uint8_t& host_setup() noexcept
{
    return ep1_ctrl();
}

inline volatile std::uint8_t& host_ep_pid() noexcept
{
    return ep2_t_len();
}

inline volatile std::uint8_t& host_rx_ctrl() noexcept
{
    return ep2_ctrl();
}

inline volatile std::uint8_t& host_tx_len() noexcept
{
    return ep3_t_len();
}

inline volatile std::uint8_t& host_tx_ctrl() noexcept
{
    return ep3_ctrl();
}

inline constexpr std::uint8_t kUcHostMode      = 0x80;
inline constexpr std::uint8_t kUcLowSpeed      = 0x40;
inline constexpr std::uint8_t kUcIntBusy       = 0x08;
inline constexpr std::uint8_t kUcDmaEn         = 0x01;

inline constexpr std::uint8_t kUhPdDis         = 0x80;
inline constexpr std::uint8_t kUhLowSpeed      = 0x04;
inline constexpr std::uint8_t kUhBusReset      = 0x02;
inline constexpr std::uint8_t kUhPortEn        = 0x01;

inline constexpr std::uint8_t kUhSofEn         = 0x40;
inline constexpr std::uint8_t kUhEpTxEn        = 0x40;
inline constexpr std::uint8_t kUhEpRxEn        = 0x08;

inline constexpr std::uint8_t kUieTransfer     = 0x02;
inline constexpr std::uint8_t kUieDetect       = 0x01;

inline constexpr std::uint8_t kUifTransfer     = 0x02;
inline constexpr std::uint8_t kUifDetect       = 0x01;

inline constexpr std::uint8_t kUisToggleOk     = 0x40;
inline constexpr std::uint8_t kMaskUisHRes     = 0x0F;

inline constexpr std::uint8_t kUmsDeviceAttach = 0x01;
inline constexpr std::uint8_t kUmsDmLevel      = 0x02;

inline constexpr std::uint8_t kUepRTog         = 0x80;
inline constexpr std::uint8_t kUepTTog         = 0x40;
inline constexpr std::uint8_t kUepAutoTog      = 0x10;

inline constexpr std::uint8_t kPidSetup        = 0x0D;
inline constexpr std::uint8_t kPidIn           = 0x09;
inline constexpr std::uint8_t kPidOut          = 0x01;
inline constexpr std::uint8_t kPidNak          = 0x0A;
inline constexpr std::uint8_t kPidStall        = 0x0E;
inline constexpr std::uint8_t kPidData0        = 0x03;
inline constexpr std::uint8_t kPidData1        = 0x0B;

} // namespace usb2reg

} // namespace ch582
