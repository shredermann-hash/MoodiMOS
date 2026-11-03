#!/bin/sh
# Commands for robot interaction:
# - Voice input/output: Uses torso microphone → torso speaker (`brain/robot_conversation.py`).
#   - Run `sh brain/talk_to_robot.sh` → Mac listens, processes, speaks; USB-connected robot or
#     UNO Q hub relaying torso port.
#   - Run `sh brain/talk_to_robot.sh uno-q` → No Mac: all on UNO Q (`gemma3:1b`, significantly slower).
#   - `UNO_Q=arduino@adresse…` → Robot on this UNO Q hub, SSH-accessible via key; defaults to Mac service key (`brain/mac_service.sh`), else none.
# 
# Torso screen button trigger:
# - Initiates exchange. Without terminal, `brain/mac_service.sh` replicates behavior; if running, it leases the port for the event.
# 
# Mac Ollama:
# - Listens only to `127.0.0.1` (localhost); no network exposure.
set -eu
cd "$(dirname "$0")/.."
IA=${MOODIMOS_IA:-$HOME/MoodiMOS-IA}
plist="$HOME/Library/LaunchAgents/fr.moodimos.conversation.plist"
host=${UNO_Q-$(plutil -extract EnvironmentVariables.MOODIMOS_UNO_Q raw "$plist" 2>/dev/null || true)}
modele=${MOODIMOS_MODELE:-ministral-3:3b}
if [ -n "$host" ]; then
  ssh -o BatchMode=yes "$host" 'mkdir -p ~/moodimos'
  scp -q brain/robot_conversation.py brain/conversation_test.py brain/robot_voice.py \
    $(ls brain/people.json 2>/dev/null) "$host:moodimos/"
fi
if [ "${1:-}" = uno-q ]; then
  [ -n "$host" ] || { echo "Which UNO Q? UNO_Q=arduino@address sh brain/talk_to_robot.sh uno-q" >&2; exit 2; }
  exec ssh -t "$host" 'TERM=xterm ~/moodimos/venv-ia/bin/python ~/moodimos/robot_conversation.py'
fi
if ! curl -s -m 1 http://127.0.0.1:11434/api/version >/dev/null; then
  echo "Starting the Mac brain (Ollama)"
  OLLAMA_MODELS="$IA/ollama-modeles" OLLAMA_HOST=127.0.0.1:11434 nohup "$IA/ollama/ollama" serve \
    > "$IA/ollama.log" 2>&1 < /dev/null &
  for _ in $(seq 30); do curl -s -m 1 http://127.0.0.1:11434/api/version >/dev/null && break; sleep 1; done
fi
echo "Loading $modele on Mac (first time from disk: up to 1 min 30)."
curl -s http://127.0.0.1:11434/api/generate -d "{\"model\":\"$modele\",\"keep_alive\":\"30m\"}" >/dev/null
MOODIMOS_UNO_Q=$host MOODIMOS_MODELE=$modele exec "$IA/venv-cerveau/bin/python" brain/robot_conversation.py
