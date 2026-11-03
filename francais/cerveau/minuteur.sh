#!/bin/sh
# Le minuteur du robot depuis le Mac (édition complète), sans passer par la voix (cerveau/conversation_robot.py).
#   sh cerveau/minuteur.sh 20          20 minutes
#   sh cerveau/minuteur.sh 1h30        une durée écrite : 1h30, 90 s, « une heure et demie »
#   sh cerveau/minuteur.sh fin         annule, ou arrête la sonnerie
#   sh cerveau/minuteur.sh             ce qu'il reste
set -eu
cd "$(dirname "$0")/.."
IA=${MOODIMOS_IA:-$HOME/MoodiMOS-IA}
plist="$HOME/Library/LaunchAgents/fr.moodimos.conversation.plist"
host=${UNO_Q-$(plutil -extract EnvironmentVariables.MOODIMOS_UNO_Q raw "$plist" 2>/dev/null || true)}
MOODIMOS_UNO_Q=$host exec "$IA/venv-cerveau/bin/python" cerveau/conversation_robot.py minuteur "$@"
