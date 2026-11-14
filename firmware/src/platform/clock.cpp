#include "ch582/platform/clock.hpp"

#include "ch582/compiler.hpp"
#include "ch582/platform/registers.hpp"

namespace ch582::clock
{
namespace
{

void wait_cycles(volatile std::uint32_t count) noexcept
{
    while (count-- != 0)
    {
        nop();
    }
}

} // namespace

CH582_HIGH_CODE void init() noexcept
{
    // The Flasher board has no external crystal load capacitors. The CH582
    // datasheet recommends C_LOAD = 111b (24 pF) for this case.
    {
        sysreg::SafeAccess access;
        sysreg::xt32m_tune() = static_cast<std::uint8_t>((sysreg::xt32m_tune() & 0x8Fu) | 0x70u);
    }

    // HSE power on.
    if ((sysreg::hfck_power_ctrl() & sysreg::kClockXt32mOn) == 0)
    {
        sysreg::SafeAccess access;
        sysreg::hfck_power_ctrl() |= sysreg::kClockXt32mOn;
    }
    wait_cycles(2400);

    // Vendor startup setting for the clock tree, applied while still at the
    // reset clock before switching to 80 MHz.
    {
        sysreg::SafeAccess access;
        sysreg::misc_ctrl() = (sysreg::misc_ctrl() & 0xF9FFFFF8u) | 0x60000005u;
    }

    // PLL 480 MHz power on and wait for lock.
    {
        sysreg::SafeAccess access;
        sysreg::pll_config() &= static_cast<std::uint8_t>(~(1u << 5));
        sysreg::hfck_power_ctrl() |= sysreg::kClockPllOn;
    }
    wait_cycles(4000);

    // 480 MHz / 6 = 80 MHz. Set the flash wait state in the same RAM-resident
    // sequence, before any instruction fetch from flash at the new speed.
    {
        sysreg::SafeAccess access;
        sysreg::clock_sys_cfg() = static_cast<std::uint16_t>((1u << 6) | kPll);
        sysreg::flash_cfg()     = (kPll == 8u) ? 0x52 : 0x02;
        sysreg::pll_config() |= 0x80;
    }


    systick::now()  = 0;
    systick::ctrl() = systick::kCtrlEnable;
}

} // namespace ch582::clock
