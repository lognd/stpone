# DEC-014 -- First bench calibration: wheel 4.0462 in, 96 in coast, slack 378, bumps sized by 3F/2

> Amended by [DEC-015](DEC-015-one-bump-first-bump-and-win7-settings-tag.md): the first bump is `remaining - F`.

Date: 2026-11-07. Owner: logan. Status: accepted. Basis: one bench wind of a 30 ft roll plus the
host model in `tests/unit/trim_sim.cpp`.

Supersedes (in part) [DEC-012](DEC-012-two-yard-coast-and-per-wind-learned-bumps.md) (default
`coast` 72 in -> 96 in; the full-speed bump safety rule) and
[DEC-013](DEC-013-feet-bindings-and-stop-short-slack.md) (slack 382 -> 378 pulses; wheel diameter
and the derived 30 ft / 90 ft lengths).

## Context

The first bench wind of a 30 ft roll read +6.9 in over on the machine and measured +11 in over
with a tape. The operator asked for (a) a corrected wheel radius, (b) a wind that ends near -2.0 in
so the paper can be pulled by hand, and (c) estimation that gets within about one stop/start, while
keeping the earlier requirement not to go "a crazy amount over".

## Decision

1. The measuring wheel diameter is 4.0462 in (102773 um), from the ratio 371 in / 366.9 in = 1.0112
   applied to 4.0015 in. Derived: 30 ft = 67970 pulses, 90 ft = 203911 pulses.
2. `kSlackPulses` = 378 (2.0 in at the new wheel), so the goal is 2.0 in short of the label length.
3. The default `coast` is 96 in = 18125 pulses (was 72 in): the first wind overshot its goal by
   about 8.9 in, so the main run now cuts earlier.
4. The first bump after the main run is sized `remaining - 3F/2` (F = measured full-speed
   follow-through; was `remaining - 2F`; `kFullSafetyNum/Den`). A bump starts from rest and coasts
   no further than F; the extra half F covers friction jitter. Probe/ratio sizing is unchanged.

## Consequences

- Boards with saved settings keep the old wheel/coast: send `DEFAULTS` then `SAVE`.
- The wheel correction rests on a single measurement and the 96 in coast is inferred from one
  run; re-check both on the next bench winds (calibration runbook).
- Model trade-off (`trim_sim`): sizing by exactly F finished in about 3 to 4 bumps on average but
  overshot by up to 3081 pulses in the model, against the 800 pulse gate, so it was rejected in
  favour of 3/2 F. The bump count is therefore not guaranteed to fall to one stop/start; the win
  is the larger first bump, and the learned follow-through still makes later bumps small.
- Spec L2/L3 prose that quotes the old numbers is updated where listed in this change; this
  record governs where they differ.
