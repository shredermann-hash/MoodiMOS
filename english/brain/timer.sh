#!/bin/sh
# Robot timer from Mac (complete edition), bypassing voice (brain/robot_conversation.py):
# ```
# sh brain/timer.sh 20          20 minutes
# sh brain/timer.sh 1h30        written duration: 1h30, 90 s, "one hour thirty"
# sh brain/timer.sh fin         cancel/stop the alarm
# sh brain/timer.sh             remaining time
# ```
set -eu
cd "$(dirname "$0")/.."
IA=${MOODIMOS_IA:-$HOME/MoodiMOS-IA}
plist="$HOME/Library/LaunchAgents/fr.moodimos.conversation.plist"
host=${UNO_Q-$(plutil -extract EnvironmentVariables.MOODIMOS_UNO_Q raw "$plist" 2>/dev/null || true)}
MOODIMOS_UNO_Q=$host exec "$IA/venv-cerveau/bin/python" brain/robot_conversation.py minuteur "$@"
