#pragma once
// frob:waive PARSE002 reason="C++-only header kept as .h to match the include convention"
// Incremental LCD repaint (DEC-009): the panel shadow plus a bounded diff pump. The LCD bus is
// ~10 kHz, so one character costs ~9 ms and a full rewrite ~0.8 s; the pump therefore writes at
// most kPanelBudget cell-times per call and carries the rest to the next main-loop pass, so the
// controller, the host link and the watchdog run between chunks. Pure and templated on the
// output `Io`, so tests/unit/test_panel.py drives the exact code the AVR runs.

#include <stdint.h>

#include <stpalpha/machine.h>

namespace stpalpha {

    constexpr uint8_t kPanelRows = 4;
    constexpr uint8_t kPanelCols = 20;
    // Cell-times per pump call (a character is 1, a cursor move is 1): 5 * ~7.8 ms = ~39 ms at 9.9 kHz
    // (one nibble transfer is ~3.9 ms: start, address and 3 bytes of 9 clocks at 101 us).
    constexpr uint8_t kPanelBudget = 5;
    constexpr uint8_t kPanelClasses = 3;
    // Every this-many-th pump serves the lowest priority class first (starvation guard).
    constexpr uint8_t kLowSlotEvery = 2;
    // Shadow value for "unknown": never equal to a wanted (ASCII) character, so it is rewritten.
    constexpr char kUnknownCell = static_cast<char>(0xFF);

    // frob:doc docs/spec/L5-component-design/SUB-03-firmware-app.md#comp-0306
    // Shadow of the panel contents and the bounded repaint. `Io` supplies:
    //   void move(uint8_t col, uint8_t row);  void put(char c);
    //   bool faulted();      (the bus timed out: stop at once)
    //   bool interrupted();  (a stop/pause toggle is pending: leave the rest for later)
    template <class Io> class Panel {
      public:
        Panel() {
            invalidate();
        }

        // The panel may hold anything now (init, heal, bus fault): rewrite every cell.
        void invalidate() {
            for (auto& row : shown_) {
                for (auto& cell : row) {
                    cell = kUnknownCell;
                }
            }
            cursor_ = kNoCursor;
        }

        // Write the cells of `want` that differ from the shadow, in priority order (cell_class()),
        // at most `budget` cell-times. Every kLowSlotEvery-th call serves the classes in REVERSE
        // order, so the low-priority cells (state row, hints, heal rewrites) are guaranteed a
        // whole chunk even under continuous percent/length changes. True when the panel now shows
        // `want` completely. Rows are NUL-terminated and padded with spaces.
        bool pump(const LCDPacket& want, Io& io, uint8_t budget = kPanelBudget) {
            const bool low_first = (++pass_ % kLowSlotEvery) == 0;
            for (uint8_t i = 0; i < kPanelClasses; ++i) {
                const uint8_t cls = low_first ? static_cast<uint8_t>(kPanelClasses - 1 - i) : i;
                if (!pump_class(want, io, budget, cls)) {
                    return false;
                }
            }
            return true;
        }

        // Priority class of a cell (0 = served first): the percent number and the length digits
        // of the winding screen, then the bar, then everything else.
        static constexpr uint8_t cell_class(uint8_t row, uint8_t col) {
            if ((row == 1 && col <= 2) || (row == 2 && col >= 4 && col <= 9)) {
                return 0;
            }
            if (row == 1 && col >= 6 && col <= 18) {
                return 1;
            }
            return 2;
        }

        // Forget where the HD44780 address counter is (after any command not sent by pump()).
        void lose_cursor() {
            cursor_ = kNoCursor;
        }

      private:
        // Write the pending cells of one class; false when the chunk is used up or must yield.
        bool pump_class(const LCDPacket& want, Io& io, uint8_t& budget, uint8_t cls) {
            for (uint8_t i = 0; i < kPanelRows; ++i) {
                const uint8_t r = row_at(i);
                bool ended = false;
                for (uint8_t c = 0; c < kPanelCols; ++c) {
                    ended = ended || want.rows[r][c] == '\0';
                    const char ch = ended ? ' ' : want.rows[r][c];
                    if (cell_class(r, c) != cls || ch == shown_[r][c]) {
                        continue;
                    }
                    const uint8_t pos = static_cast<uint8_t>((r << 5) | c);
                    const uint8_t cost = cursor_ == pos ? 1 : 2;
                    if (budget < cost || io.faulted() || io.interrupted()) {
                        return false;
                    }
                    if (cursor_ != pos) {
                        io.move(c, r);
                    }
                    io.put(ch);
                    if (io.faulted()) {
                        cursor_ = kNoCursor; // that character may not have arrived
                        return false;
                    }
                    shown_[r][c] = ch;
                    // The address counter advances by itself; past the row end it is undefined.
                    cursor_ = c + 1 < kPanelCols ? static_cast<uint8_t>(pos + 1) : kNoCursor;
                    budget = static_cast<uint8_t>(budget - cost);
                }
            }
            return true;
        }
        // Repaint priority: percent done and length (rows 1, 2) before the static rows.
        static constexpr uint8_t row_at(uint8_t i) {
            return i == 0 ? 1 : i == 1 ? 2 : i == 2 ? 0 : 3;
        }
        static constexpr uint8_t kNoCursor = 0xFF;
        char shown_[kPanelRows][kPanelCols];
        uint8_t pass_ = 0;
        uint8_t cursor_ = kNoCursor; // (row << 5) | col the address counter points at
    };

} // namespace stpalpha
