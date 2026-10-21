// Host driver for include/stpalpha/panel.h: a virtual HD44780 (4x20 DDRAM with an auto-incrementing
// address counter) behind stpalpha::Panel. Commands on stdin, one result line each:
//   row <r> <text>      set the wanted text of row r (padded by the panel)
//   pump [budget] [screen]  run one pump (and print the screen after it); prints "sync=<0|1> puts=<n> moves=<n>"
//   invalidate          forget the shadow
//   interrupt <0|1>     a stop toggle is pending (the pump must yield)
//   fault <n>           the bus times out on the n-th put from now (0 = never)
//   corrupt <r> <c> <ch>  glitch one cell of the real panel behind the shadow's back
//   screen              prints the four panel rows, "|"-delimited
#include <stdio.h>
#include <string.h>

#include <stpalpha/panel.h>

using namespace stpalpha;

struct VirtualLcd {
    char cells[kPanelRows][kPanelCols + 1];
    int row = 0, col = 0;
    int puts = 0, moves = 0;
    bool fault = false;
    bool pending = false;
    int fault_after = 0;

    VirtualLcd() {
        for (auto& r : cells) {
            memset(r, '?', kPanelCols);
            r[kPanelCols] = '\0';
        }
    }
    void move(uint8_t c, uint8_t r) {
        ++moves;
        col = c;
        row = r;
    }
    void put(char ch) {
        if (fault_after > 0 && --fault_after == 0) {
            fault = true;
            return;
        }
        ++puts;
        if (row < kPanelRows && col < kPanelCols) {
            cells[row][col] = ch;
        }
        ++col;
    }
    bool faulted() {
        return fault;
    }
    bool interrupted() {
        return pending;
    }
};

int main() {
    VirtualLcd lcd;
    Panel<VirtualLcd> panel;
    LCDPacket want;
    memset(&want, 0, sizeof(want));
    char line[128];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\r\n")] = '\0';
        int r = 0, c = 0;
        char ch = 0;
        if (sscanf(line, "row %d", &r) == 1 && r >= 0 && r < kPanelRows && strlen(line) >= 6) {
            strncpy(want.rows[r], line + 6, kPanelCols); // NUL-terminated by the memset
            want.rows[r][kPanelCols] = '\0'; // "row N " then the text
            puts("ok");
        } else if (!strncmp(line, "pump", 4)) {
            int budget = kPanelBudget;
            sscanf(line + 4, "%d", &budget); // "pump screen" keeps the default budget
            lcd.puts = lcd.moves = 0;
            bool sync = panel.pump(want, lcd, static_cast<uint8_t>(budget));
            printf("sync=%d puts=%d moves=%d\n", sync ? 1 : 0, lcd.puts, lcd.moves);
            if (strstr(line, "screen")) {
                for (auto& row : lcd.cells) {
                    printf("|%s|", row);
                }
                puts("");
            }
        } else if (!strcmp(line, "invalidate")) {
            panel.invalidate();
            puts("ok");
        } else if (sscanf(line, "interrupt %d", &r) == 1) {
            lcd.pending = r != 0;
            puts("ok");
        } else if (sscanf(line, "fault %d", &r) == 1) {
            lcd.fault_after = r;
            lcd.fault = false;
            puts("ok");
        } else if (sscanf(line, "corrupt %d %d %c", &r, &c, &ch) == 3) {
            lcd.cells[r][c] = ch;
            puts("ok");
        } else if (!strcmp(line, "screen")) {
            for (auto& row : lcd.cells) {
                printf("|%s|", row);
            }
            puts("");
        }
    }
    return 0;
}
