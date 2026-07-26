#!/system/bin/sh
# 4-speaker + rotation helper for Samsung tablets with four Cirrus CS35L41 amps
# (Galaxy Tab S6 SM-T860/T865 and similar). Points all four amps at the stereo
# slots (so every speaker plays) and assigns L/R per display rotation so the
# stereo image stays correct in every orientation. One-shot: run once per
# rotation change (treble_app drives persist.sys.phh.audio_rotation). Self-noops
# on hardware without these controls.
export PATH=/system/bin:$PATH

FL="FL ASPRX1 Slot Position"; FR="FR ASPRX1 Slot Position"
RL="RL ASPRX1 Slot Position"; RR="RR ASPRX1 Slot Position"
tinymix "$FL" >/dev/null 2>&1 || exit 0   # not this hardware

rot="${1:-$(getprop persist.sys.phh.audio_rotation)}"
# slot 0 = Left channel, slot 1 = Right channel.
# amp positions: FL=bottom-left FR=bottom-right RL=top-left RR=top-right.
case "$rot" in
    0) set -- 0 1 0 1 ;;   # portrait:          L = left column  (FL,RL)
    1) set -- 1 1 0 0 ;;   # landscape:         L = top row      (RL,RR)
    2) set -- 1 0 1 0 ;;   # portrait flipped:  L = right column (FR,RR)
    3) set -- 0 0 1 1 ;;   # landscape:         L = bottom row   (FL,FR)
    *) exit 0 ;;
esac
tinymix "$FL" "$1" >/dev/null 2>&1; tinymix "$FR" "$2" >/dev/null 2>&1
tinymix "$RL" "$3" >/dev/null 2>&1; tinymix "$RR" "$4" >/dev/null 2>&1
