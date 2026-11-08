# Mean Module

A gritty analog-style monosynth for Ableton Move (Schwung sound generator): two oscillators plus noise, a
saturating 2-pole multimode filter that squeezes its resonance out when driven hot and self-oscillates, two
exponential ADSRs, two tempo-syncable LFOs, a six-slot mod matrix, a joystick emulation, an arpeggiator, a four-knob touch FX bank, FM/x-mod, and a post-filter drive. 214 factory presets, full state save/recall.

New to it? Play a note, turn **Grit** on the Main page, jump around with **Cat**, then open the **FX Bank** page: hold a knob to play its effect, turn it for more, double-tap it for the next one. `docs/UI_GUIDE.md` has the page map; `CHANGELOG.md` has what changed.

Build: `bash scripts/build.sh` (Docker). Test: `bash scripts/test.sh`. Install: `bash scripts/install.sh`
(`MOVE_HOST=user@host` to override `ableton@move.local`). MIT licensed.
