#!/bin/sh
# Mac Conversation Service (`brain/robot_conversation.py` service): A launchd agent launched on session open, restarted if it fails. Connects to the robot as soon as it’s plugged via USB; the torso screen’s bubble button triggers interaction without a Mac user. Gray bubble: Mac does not respond.
# 
# ```
# sh brain/mac_service.sh installer    installs and starts the agent (re-run if the IA folder changes)
# UNO_Q=arduino@address sh ... installer   deploys the robot on this UNO Q hub, forwarding the torso port (SSH key-based); retain `UNO_Q=` for subsequent installs to revert to Mac
# sh brain/mac_service.sh retirer       stops and removes it
# sh brain/mac_service.sh journal       logs its actions: connections, questions, and replies (Ctrl-C to exit)
# sh brain/mac_service.sh etat          checks if it’s running
# ```
# 
# Mac Ollama only listens to itself (127.0.0.1): No network ports open.
set -eu
cd "$(dirname "$0")/.."
IA=${MOODIMOS_IA:-$HOME/MoodiMOS-IA}
nom=fr.moodimos.conversation
plist="$HOME/Library/LaunchAgents/$nom.plist"
journal="$HOME/Library/Logs/moodimos-conversation.log"   # On Mac: launchd starts before the disk appears.
cible="gui/$(id -u)"
uno_q=${UNO_Q-$(plutil -extract EnvironmentVariables.MOODIMOS_UNO_Q raw "$plist" 2>/dev/null || true)}
case "${1:-}" in
  install|installer)
    [ -x "$IA/venv-cerveau/bin/python" ] || { echo "No Python environment in $IA (see brain/README.md)" >&2; exit 1; }
    mkdir -p "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
    cat > "$plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$nom</string>
  <key>ProgramArguments</key>
  <array>
    <string>$IA/venv-cerveau/bin/python</string>
    <string>-u</string>
    <string>$PWD/brain/robot_conversation.py</string>
    <string>service</string>
  </array>
  <key>EnvironmentVariables</key>
  <dict>
    <key>MOODIMOS_IA</key><string>$IA</string>
    <key>MOODIMOS_UNO_Q</key><string>$uno_q</string>
    <key>MOODIMOS_MODELE</key><string>${MOODIMOS_MODELE:-ministral-3:3b}</string>
  </dict>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>30</integer>
  <!-- without which launchd throttles it as a background task: the first question took 88 s to be understood. -->
  <key>ProcessType</key><string>Interactive</string>
  <key>StandardOutPath</key><string>$journal</string>
  <key>StandardErrorPath</key><string>$journal</string>
</dict>
</plist>
EOF
    launchctl bootout "$cible/$nom" 2>/dev/null || true
    for _ in $(seq 20); do launchctl print "$cible/$nom" >/dev/null 2>&1 || break; sleep 0.5; done   # wait for the old one to stop
    launchctl bootstrap "$cible" "$plist"
    echo "Service installed; log: sh brain/mac_service.sh log"
    ;;
  remove|retirer)
    launchctl bootout "$cible/$nom" 2>/dev/null || true
    rm -f "$plist"
    echo "Service removed."
    ;;
  log|journal)
    exec tail -n 40 -f "$journal"
    ;;
  status|etat)
    if launchctl print "$cible/$nom" >/dev/null 2>&1; then
      launchctl print "$cible/$nom" | grep -E "^\s+(state|pid|last exit code) ="
    else
      echo "Service not installed."
    fi
    ;;
  *)
    echo "Usage: \`$0 install|remove|log|status\`" >&2
    exit 2
    ;;
esac
