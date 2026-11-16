#include <cstdint>

#include "ch582/app/app.hpp"
#include "ch582/isp/isp.hpp"
#include "ch582/platform/clock.hpp"
#include "ch582/platform/registers.hpp"
#include "ch582/serial/uart.hpp"
#include "ch582/usb/device.hpp"
#include "ch582/usb/host.hpp"

using namespace ch582;

namespace
{

inline constexpr std::uint32_t kLedPin  = gpio::kPin12; // PA12, active high
inline constexpr std::uint32_t kVbusPin = gpio::kPin4;  // PB4, MT9700 enable

} // namespace

int main()
{
    // Boot indicator: if the LED stays on, main was reached but clock/USB
    // initialization stalled; if it never lights, startup did not reach main.
    gpio::pa::dir() |= kLedPin;
    gpio::pa::out() |= kLedPin;

    sysreg::disable_watchdog_reset();
    clock::init();

    // Status LED and the USB-A VBUS load switch start off.
    gpio::pa::out() &= ~kLedPin;

    gpio::pb::dir() |= kVbusPin;
    gpio::pb::out() &= ~kVbusPin;

    auto&                usb  = usb::Device::instance();
    auto&                uart = serial::Uart::instance();

    static usbhost::Host host;
    static isp::Isp      isp(host);
    static app::App      app(usb, host, uart, isp);

    usb.init();
    app.init();

    enable_global_irq();

    std::uint32_t next_blink = systick::now() + 500u * clock::kTicksPerMs;
    bool          led_on     = false;

    for (;;)
    {
        app.poll();

        if (usb.enumerated())
        {
            if (!led_on)
            {
                led_on = true;
                gpio::pa::out() |= kLedPin;
            }
        }
        else
        {
            led_on = false;
            if (systick::expired(next_blink))
            {
                next_blink = systick::now() + 500u * clock::kTicksPerMs;
                gpio::pa::out() ^= kLedPin;
            }
        }
    }
}
