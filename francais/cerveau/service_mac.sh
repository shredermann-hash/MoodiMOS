#!/bin/sh
# Service de conversation du Mac (cerveau/conversation_robot.py service) : un agent de launchd, lancé à l'ouverture de
# la session et relancé s'il s'arrête. Il se relie au robot dès qu'il est branché en USB sur le Mac ; le bouton bulle
# de l'écran du torse lance alors un échange, sans personne au Mac. Bulle grise : le Mac ne répond pas.
#   sh cerveau/service_mac.sh installer     installe l'agent et le démarre (à refaire si le dossier IA change)
#   UNO_Q=arduino@adresse sh … installer    robot sur le hub de cette UNO Q, qui relaie le port du torse (SSH par
#                                           clé) ; gardée aux installations suivantes, UNO_Q= pour revenir au Mac
#   sh cerveau/service_mac.sh retirer       l'arrête et le retire
#   sh cerveau/service_mac.sh journal       ce qu'il fait : liaisons, questions et réponses (Ctrl-C pour sortir)
#   sh cerveau/service_mac.sh etat          tourne-t-il ?
# L'Ollama du Mac n'écoute que le Mac lui-même (127.0.0.1) : rien n'est ouvert sur le réseau.
set -eu
cd "$(dirname "$0")/.."
IA=${MOODIMOS_IA:-$HOME/MoodiMOS-IA}
nom=fr.moodimos.conversation
plist="$HOME/Library/LaunchAgents/$nom.plist"
journal="$HOME/Library/Logs/moodimos-conversation.log"   # sur le Mac : launchd l'ouvre avant que le disque soit là
cible="gui/$(id -u)"
uno_q=${UNO_Q-$(plutil -extract EnvironmentVariables.MOODIMOS_UNO_Q raw "$plist" 2>/dev/null || true)}
case "${1:-}" in
  installer)
    [ -x "$IA/venv-cerveau/bin/python" ] || { echo "pas d'environnement Python dans $IA (cerveau/LISEZMOI.md)" >&2; exit 1; }
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
    <string>$PWD/cerveau/conversation_robot.py</string>
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
  <!-- sans quoi launchd le bride comme une tâche de fond : la première question mettait 88 s à être comprise -->
  <key>ProcessType</key><string>Interactive</string>
  <key>StandardOutPath</key><string>$journal</string>
  <key>StandardErrorPath</key><string>$journal</string>
</dict>
</plist>
EOF
    launchctl bootout "$cible/$nom" 2>/dev/null || true
    for _ in $(seq 20); do launchctl print "$cible/$nom" >/dev/null 2>&1 || break; sleep 0.5; done   # l'ancien s'arrête
    launchctl bootstrap "$cible" "$plist"
    echo "Service installé ; journal : sh cerveau/service_mac.sh journal"
    ;;
  retirer)
    launchctl bootout "$cible/$nom" 2>/dev/null || true
    rm -f "$plist"
    echo "Service retiré."
    ;;
  journal)
    exec tail -n 40 -f "$journal"
    ;;
  etat)
    if launchctl print "$cible/$nom" >/dev/null 2>&1; then
      launchctl print "$cible/$nom" | grep -E "^\s+(state|pid|last exit code) ="
    else
      echo "service non installé"
    fi
    ;;
  *)
    echo "usage : $0 installer|retirer|journal|etat" >&2
    exit 2
    ;;
esac
