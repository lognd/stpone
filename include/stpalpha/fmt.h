#pragma once
// frob:waive PARSE002 reason="C++-only header kept as .h to match the include convention"
// Format strings live in flash on the AVR (DEC-010): a literal in .data costs RAM as well as flash and
// the ATmega32U4 has only 2560 bytes. Write STP_SNPRINTF(out, cap, STP_FMT("fmt"), ...): snprintf_P with a
// PSTR format on the AVR, plain snprintf on the host, so the very same source runs in both places.

#include <stdio.h>

#ifdef __AVR__
#include <avr/pgmspace.h>
#define STP_FMT(s) PSTR(s)
#define STP_SNPRINTF snprintf_P
#else
#define STP_FMT(s) s
#define STP_SNPRINTF snprintf
#endif
