# Deploying to the Move (no SSH needed)

Verified 2026-11-07 against Schwung host v1.9.2, Move at a DHCP address on the same WiFi as this PC.

1. Build + package: `bash scripts/build.sh` (Docker; add `/c/Program Files/Docker/Docker/resources/bin` to PATH in Git Bash).
   Produces `dist/meanmodule-module.tar.gz` with explicit modes (dirs 0755, dsp.so 0755, others 0644, owner 0).
   dsp.so needs glibc <= 2.27 (checked with `objdump -T`).
2. Install through Schwung Manager (port 7700) with the user's script (outside this repo, read it before running):
   ```
   powershell -NoProfile -File C:\Users\rcf79\OneDrive\Documents\Projects\AudioStreamTest\deploy_to_move.ps1 -Tarball dist\meanmodule-module.tar.gz -ModuleId meanmodule
   ```
   It re-resolves the Move's IPv4 with `ping -4 move.local` (the address changes), uploads via `/modules/install-custom`,
   and checks the module is listed. Success text: "Installed meanmodule from tarball".
3. Verify placement: `http://<ip>:7700/files?path=/data/UserData/schwung/modules/sound_generators/meanmodule`
   (confirmed: Manager puts sound generators under `sound_generators/<id>/` from `component_type`).
4. The user must restart the Move (or Schwung) and load the module in a slot; the old build stays in memory otherwise.

Alternative: Manager -> Install Custom Module -> From GitHub URL (`richardfuller/MeanModule`) reads `release.json` on `main`
and downloads the tarball attached to the matching GitHub release. Bump `version` in BOTH `src/module.json` and
`release.json`, upload the new tarball to the release, then install.

Gotchas: use `curl.exe` (not the PowerShell `curl` alias); `move.local` may resolve to IPv6 first; Windows `tar` makes
read-only dirs that break Manager extraction (we build the tarball inside Docker, which is fine).
