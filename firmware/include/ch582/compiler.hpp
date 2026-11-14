#pragma once

#if defined(__GNUC__)
  #define CH582_HIGH_CODE __attribute__((section(".highcode")))
  #define CH582_INTERRUPT __attribute__((interrupt("WCH-Interrupt-fast")))
  #define CH582_PACKED    __attribute__((packed))
#else
  #define CH582_HIGH_CODE
  #define CH582_INTERRUPT
  #define CH582_PACKED
#endif
