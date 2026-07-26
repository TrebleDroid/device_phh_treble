#!/system/bin/sh
# 4-speaker + rotation fix for Samsung tablets with four Cirrus CS35L41 amps
# (Galaxy Tab S6 SM-T860/T865 and similar). Under a GSI only two of the four
# speakers play, because the two "rear" amps default to reading empty TDM
# slots, and nothing re-maps L/R when the display rotates. This points all
# four amps at the stereo slots (so every speaker plays) and reassigns L/R
# per display rotation so the stereo image stays correct in every orientation.
# Self-noop on hardware that lacks these controls.
export PATH=/system/bin:$PATH

FL="FL ASPRX1 Slot Position"; FR="FR ASPRX1 Slot Position"
RL="RL ASPRX1 Slot Position"; RR="RR ASPRX1 Slot Position"

# Wait for the audio mixer to come up, then require the CS35L41 quad amps.
i=0
while [ "$i" -lt 60 ]; do
    if tinymix >/dev/null 2>&1; then
        tinymix "$FL" >/dev/null 2>&1 || exit 0   # mixer up, but not this hardware
        break
    fi
    i=$((i + 1)); sleep 1
done
tinymix "$FL" >/dev/null 2>&1 || exit 0

set_slots() {
    tinymix "$FL" "$1" >/dev/null 2>&1; tinymix "$FR" "$2" >/dev/null 2>&1
    tinymix "$RL" "$3" >/dev/null 2>&1; tinymix "$RR" "$4" >/dev/null 2>&1
}

# slot 0 = Left channel, slot 1 = Right channel.
# amp positions: FL = bottom-left, FR = bottom-right, RL = top-left, RR = top-right.
last=-1
while true; do
    r=$(dumpsys window 2>/dev/null | grep -oE 'mRotation=[0-9]' | head -1 | cut -d= -f2)
    case "$r" in
        0) set_slots 0 1 0 1 ;;   # portrait:          L = left column  (FL,RL)
        1) set_slots 1 1 0 0 ;;   # landscape:         L = top row      (RL,RR)
        2) set_slots 1 0 1 0 ;;   # portrait flipped:  L = right column (FR,RR)
        3) set_slots 0 0 1 1 ;;   # landscape:         L = bottom row   (FL,FR)
        *) sleep 1; continue ;;
    esac
    [ "$r" != "$last" ] && { log -t phh-spkrot "rotation $r"; last="$r"; }
    sleep 0.5
done
