// Host driver for include/stpalpha/hostlink.h: reads command lines on stdin,
// prints one reply per line (and the REQ flags after each as "#req=N").
#include <stdio.h>
#include <string.h>

#include <stpalpha/hostlink.h>
#include <stpalpha/params_image.h>
#include <stpalpha/speed.h>

using namespace stpalpha;

int main() {
    Controller c;
    HostLink h;
    h.sync(c);
    Snapshot s{};
    s.count = -42;
    s.errors = 3;
    s.speed = 5170;
    s.btn = 0b1010;
    s.toggles[0] = 7;
    s.pending[2] = 1;
    s.enc_dir = -1;
    s.uptime_ms = 1234;
    s.stack_free = 321;
    s.pass_max_ms = 47;
    s.isr_lat_max = 33;
    SpeedMeter meter;
    PassMeter pass;
    char line[96];
    char out[512];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!strcmp(line, "@wind")) { // test hook: start a panel wind
            Inputs in{};
            in.pending = kBtn1;
            in.down_after = kBtn1;
            c.step(in);
            puts("@ok");
            continue;
        }
        if (!strncmp(line, "@meter ", 7) || !strncmp(line, "@rebase ", 8)) {
            // test hook: @meter <now_ms> <count> samples; @rebase <now_ms> <count> restarts the window
            bool rebase = line[1] == 'r';
            unsigned long now = 0;
            long count = 0;
            sscanf(line + (rebase ? 8 : 7), "%lu %ld", &now, &count);
            if (rebase) {
                meter.rebase(static_cast<uint32_t>(now), static_cast<int32_t>(count));
            } else {
                meter.update(static_cast<uint32_t>(now), static_cast<int32_t>(count));
            }
            printf("@speed=%lu\n", static_cast<unsigned long>(meter.pulses_per_s()));
            continue;
        }
        if (!strncmp(line, "@pass ", 6)) { // test hook: the loop pass started at <now_ms>
            pass.tick(static_cast<uint32_t>(strtoul(line + 6, nullptr, 10)));
            printf("@passmax=%u\n", pass.max_ms());
            continue;
        }
        if (!strcmp(line, "@passclear")) {
            pass.clear();
            printf("@passmax=%u\n", pass.max_ms());
            continue;
        }
        if (!strcmp(line, "@worst")) { // test hook: every DUMP field at its widest
            s.count = INT32_MIN;
            s.speed = 0xFFFFFFFFu;
            s.errors = 0xFFFF;
            s.btn = 0xF;
            for (int i = 0; i < 4; ++i) {
                s.toggles[i] = 255;
                s.pending[i] = 255;
            }
            s.enc_dir = -1;
            s.uptime_ms = 0xFFFFFFFFu;
            s.stack_free = 0xFFFF;
            s.pass_max_ms = 0xFFFF;
            s.isr_lat_max = 0xFFFF;
            puts("@ok");
            continue;
        }
        if (!strncmp(line, "@speed ", 7)) { // test hook: set the snapshot speed
            s.speed = static_cast<uint32_t>(strtoul(line + 7, nullptr, 10));
            puts("@ok");
            continue;
        }
        if (!strncmp(line, "@time ", 6)) { // test hook: set the snapshot uptime
            s.uptime_ms = static_cast<uint32_t>(strtoul(line + 6, nullptr, 10));
            puts("@ok");
            continue;
        }
        if (!strncmp(line, "@image", 6)) { // test hook: EEPROM image checks on the current params
            // @image = round trip; @imagev5 = fw<=5 tag; @imagecrc = flipped bit; @imagemagic = junk tag
            Stored img = make_stored(h.params);
            if (!strcmp(line, "@imagev5")) {
                img.magic = kEepromMagicV5;
                img.crc = stored_crc(img);
            } else if (!strcmp(line, "@imagecrc")) {
                img.params.margin ^= 1u;
            } else if (!strcmp(line, "@imagemagic")) {
                img.magic = 0xFFFFFFFFu;
                img.crc = stored_crc(img);
            }
            Params out;
            LoadStatus st = check_stored(img, &out);
            printf("@image=%d size=%u same=%d\n", static_cast<int>(st),
                   static_cast<unsigned>(sizeof(Stored)),
                   (st == LoadStatus::OK && !memcmp(&out, &h.params, sizeof(Params))) ? 1 : 0);
            continue;
        }
        if (!strncmp(line, "@step ", 6)) { // test hook: @step <now_ms> <count> <pending mask> [cut]
            unsigned long now = 0, count = 0, pend = 0, cut = 0;
            sscanf(line + 6, "%lu %lu %lu %lu", &now, &count, &pend, &cut);
            Inputs in{};
            in.now_ms = static_cast<uint32_t>(now);
            in.count = static_cast<uint32_t>(count);
            in.pending = static_cast<uint8_t>(pend);
            in.down_after = static_cast<uint8_t>(pend);
            in.cut = cut != 0;
            Outputs o = c.step(in);
            printf("@debug=%d relay=%d run=%d run_stopped=%d at_stop=%lu exit=%d reset=%d leds=%u "
                   "stop_at=%lu result=%d changed=%d stopped=%d state=%d bumps=%u trim=%d outcome=%d "
                   "beep=%d manual=%d alarm=%d\n",
                   c.debug() ? 1 : 0, o.relay ? 1 : 0, c.running() ? 1 : 0, o.run_stopped ? 1 : 0,
                   static_cast<unsigned long>(o.at_stop), static_cast<int>(o.debug_exit),
                   o.reset_count ? 1 : 0, o.leds, static_cast<unsigned long>(c.stop_at()),
                   static_cast<int>(c.result()), o.changed ? 1 : 0, o.stopped ? 1 : 0,
                   static_cast<int>(c.state()), static_cast<unsigned>(c.bumps()),
                   c.trimming() ? 1 : 0, static_cast<int>(c.outcome()), static_cast<int>(o.beep),
                   c.manual() ? 1 : 0, c.alarm() ? 1 : 0);
            continue;
        }
        h.handle(c, s, line, out, sizeof(out));
        printf("%s\n#req=%u\n", out, h.requests);
        h.requests = REQ_NONE;
    }
    return 0;
}
