#pragma once
// frob:waive PARSE002 reason="C++-only header kept as .h to match the include convention"
// The persisted parameter image (EEPROM): magic + Params + CRC-16. Pure, so the host test
// (tests/unit/test_hostlink.py) round-trips, corrupts and version-checks the exact bytes the AVR
// stores (DEC-010: fw 6 added `margin` and `bump`, so the magic changed and a fw<=5 image is
// refused -- the machine then keeps the compiled-in defaults and LOAD answers ERR no_data).

#include <stddef.h>
#include <stdint.h>

#include <stpalpha/hostlink.h>

namespace stpalpha {

    // Image tag of this layout ("WIN6"), and the one fw<=5 wrote ("WIND", 26-byte image).
    constexpr uint32_t kEepromMagic = 0x57494E36u;
    constexpr uint32_t kEepromMagicV5 = 0x57494E44u;

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0309
    // What is stored: tag, parameters, CRC over both.
    struct Stored {
        uint32_t magic;
        Params params;
        uint16_t crc;
    };

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0309
    // Why a stored image was or was not used.
    enum class LoadStatus : uint8_t { OK, OLD_VERSION, BAD_MAGIC, BAD_CRC, INVALID };

    // CRC-16/ARC step, identical to avr-libc's _crc16_update (poly 0xA001, LSB first).
    inline uint16_t crc16_update(uint16_t crc, uint8_t a) {
        crc ^= a;
        for (uint8_t i = 0; i < 8; ++i) {
            crc = (crc & 1u) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001u)
                             : static_cast<uint16_t>(crc >> 1);
        }
        return crc;
    }

    // CRC-16 over the magic and the parameters (catches torn writes and byte swaps).
    inline uint16_t stored_crc(const Stored& s) {
        uint16_t crc = 0xFFFF;
        const uint8_t* m = reinterpret_cast<const uint8_t*>(&s.magic);
        for (size_t i = 0; i < sizeof(s.magic); ++i) {
            crc = crc16_update(crc, m[i]);
        }
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&s.params);
        for (size_t i = 0; i < sizeof(s.params); ++i) {
            crc = crc16_update(crc, p[i]);
        }
        return crc;
    }

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0309
    // The image for `p`, CRC filled in.
    inline Stored make_stored(const Params& p) {
        Stored s{kEepromMagic, p, 0};
        s.crc = stored_crc(s);
        return s;
    }

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0309
    // Accept `s` only if tag, CRC and Params::valid() all hold; `out` is written only on OK.
    inline LoadStatus check_stored(const Stored& s, Params* out) {
        if (s.magic == kEepromMagicV5) {
            return LoadStatus::OLD_VERSION;
        }
        if (s.magic != kEepromMagic) {
            return LoadStatus::BAD_MAGIC;
        }
        if (s.crc != stored_crc(s)) {
            return LoadStatus::BAD_CRC;
        }
        if (!s.params.valid()) {
            return LoadStatus::INVALID;
        }
        *out = s.params;
        return LoadStatus::OK;
    }

} // namespace stpalpha
