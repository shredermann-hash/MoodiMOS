#!/bin/sh
# Parler au robot : il écoute par le micro du torse et répond par son haut-parleur (cerveau/conversation_robot.py).
#   sh cerveau/parler_au_robot.sh            le Mac écoute, réfléchit et parle ; robot branché en USB sur le Mac, ou
#                                            sur le hub de la UNO Q, qui relaie alors le port du torse
#   sh cerveau/parler_au_robot.sh uno-q      sans le Mac : tout sur la UNO Q (cerveau gemma3:1b, bien plus lent)
#   UNO_Q=arduino@adresse …                  robot sur le hub de cette UNO Q, joignable en SSH par clé ; par défaut
#                                            celle du service du Mac (cerveau/service_mac.sh), sinon aucune
# Le bouton bulle de l'écran du torse lance un échange. Sans terminal, le service du Mac
# (cerveau/service_mac.sh) fait la même chose ; s'il tourne, il prête le port le temps de celle-ci.
# L'Ollama du Mac n'écoute que le Mac lui-même (127.0.0.1) : rien n'est ouvert sur le réseau.
set -eu
cd "$(dirname "$0")/.."
IA=${MOODIMOS_IA:-$HOME/MoodiMOS-IA}
plist="$HOME/Library/LaunchAgents/fr.moodimos.conversation.plist"
host=${UNO_Q-$(plutil -extract EnvironmentVariables.MOODIMOS_UNO_Q raw "$plist" 2>/dev/null || true)}
modele=${MOODIMOS_MODELE:-ministral-3:3b}
if [ -n "$host" ]; then
  ssh -o BatchMode=yes "$host" 'mkdir -p ~/moodimos'
  scp -q cerveau/conversation_robot.py cerveau/essai_conversation.py cerveau/voix_robot.py \
    $(ls cerveau/personnes.json 2>/dev/null) "$host:moodimos/"
fi
if [ "${1:-}" = uno-q ]; then
  [ -n "$host" ] || { echo "quelle UNO Q ? UNO_Q=arduino@adresse sh cerveau/parler_au_robot.sh uno-q" >&2; exit 2; }
  exec ssh -t "$host" 'TERM=xterm ~/moodimos/venv-ia/bin/python ~/moodimos/conversation_robot.py'
fi
if ! curl -s -m 1 http://127.0.0.1:11434/api/version >/dev/null; then
  echo "Démarrage du cerveau du Mac (Ollama)"
  OLLAMA_MODELS="$IA/ollama-modeles" OLLAMA_HOST=127.0.0.1:11434 nohup "$IA/ollama/ollama" serve \
    > "$IA/ollama.log" 2>&1 < /dev/null &
  for _ in $(seq 30); do curl -s -m 1 http://127.0.0.1:11434/api/version >/dev/null && break; sleep 1; done
fi
echo "Chargement de $modele sur le Mac (la première fois depuis le disque : jusqu'à 1 min 30)"
curl -s http://127.0.0.1:11434/api/generate -d "{\"model\":\"$modele\",\"keep_alive\":\"30m\"}" >/dev/null
MOODIMOS_UNO_Q=$host MOODIMOS_MODELE=$modele exec "$IA/venv-cerveau/bin/python" cerveau/conversation_robot.py
