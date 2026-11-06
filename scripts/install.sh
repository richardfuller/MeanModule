#!/bin/bash
# Install Mean Module to a Move running Schwung (needs dist/ from scripts/build.sh).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOST="${MOVE_HOST:-ableton@move.local}"
DEST=/data/UserData/schwung/modules/sound_generators/meanmodule
cd "$ROOT"
[ -d dist/meanmodule ] || { echo "dist/meanmodule not found. Run scripts/build.sh first."; exit 1; }
echo "Installing to $HOST:$DEST"
ssh "$HOST" "mkdir -p $DEST"
scp -r dist/meanmodule/* "$HOST:$DEST/"
ssh "$HOST" "chmod -R a+rw $DEST"
echo "Done. Modules rescan on list; no restart needed. To remove: ssh $HOST rm -rf $DEST"
