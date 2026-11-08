# Bank files: import and export

Preset banks can be exported to a file and loaded from files, so new or edited presets reach the Move without a new build.

## Folders (on the Move)

Everything lives under `/data/UserData/schwung/mean-module/` (created when the module loads):

| Folder | Purpose |
| --- | --- |
| `banks/` | Put `.mmbank` files here. They load automatically when the module loads, and again when you pick **Reload Folder**. |
| `exports/` | **Export Bank** writes here. Files in `exports/` are never loaded, so exporting a factory bank cannot create a duplicate of it. |

Schwung Manager's file browser (`http://move.local:7700/files`, under `/data/UserData/`) can upload to and download from both folders.

## Using it

1. Open the **Bank Files** page (after Response).
2. **Files**: *Export Bank* writes the bank currently shown by the Bank knob to `exports/<Bank>.mmbank`; *Reload Folder* re-reads `banks/`.
   It returns to *Idle* by itself. **State** shows Ready, Working, Exported, Loaded or Error. **User** shows how many user banks are loaded.
3. User banks follow the factory banks on the **Bank** knob as **U1** to **U8**, in alphabetical order of file name. A file with several `bank`
   lines gives several banks. At most 8 banks (48 presets each) are loaded; empty ones are skipped. A slot with no bank cannot be selected.
4. The jog wheel browses a user bank like any other. Program Change works too: bank select 18 to 25 picks user banks 1 to 8.

To edit a factory bank: export it, download the file, edit it, upload it to `banks/` (give it another name if you want it to sit beside the original), reload.

## File format

Plain text, one setting per line. `#` starts a comment; blank lines are ignored.

```
mmbank 1
bank My Bank            # bank name (if missing, the file name is used)

preset Fat Square Lead  # starts a preset; its name is shown in the browser (up to 39 characters)
osc1_wave 4             # parameter key, then a value
cutoff 0.7
resonance 0.35
```

* Settings start from the same defaults as the factory presets, so list only what differs.
* Keys are the parameter keys (`cutoff`, `osc2_fine`, `m1_src`, ...); a full list is in `chain_params` and `docs/MOD_MATRIX.md` covers the matrix.
  Enums and integers are numbers (for example `osc1_wave 4` is Pulse); values are clamped to each parameter's range.
* Unknown keys are ignored. The FX Bank, FX Touch, Stick position, Preset Bank and the bank-file controls are never part of a sound and are ignored too.
* Up to 96 settings per preset.
* `banks/Example.mmbank` in the repository is a small working example.

## How it works (and what to know)

* A background thread does all file work; the audio path never touches files. Requests are picked up within about 50 ms.
* The module keeps itself loaded in memory so that thread can never outlive its code. After installing a new build, restart the Move.
* A reload publishes a new set of banks atomically. A preset you were playing keeps sounding as it was until you pick another.
* The saved state stores a user preset by its position (bank number and place); if the files change, the name shown may differ, but all
  sound parameters are saved with the state, so the sound itself is unchanged.
