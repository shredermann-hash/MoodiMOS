# MoodiMOS — task-completion announcement: the fixed French message “Claude a terminé le travail demandé”.
# 
# Called by Claude Code's Stop hook, with JSON on standard input at the end of a response,
# and by Codex's notify setting in ~/.codex/config.toml, with JSON in the final argument.
# If the torso is on the Mac USB port or the UNO Q hub, the firmware's annonce command plays
# the phrase and triggers joy. For a UNO Q, the Mac sends the audio through SSH; this same script,
# running there in robot mode, relays it to the torso. Otherwise the Mac speakers are used.
# 
#   … announcement.py claude          (Claude Code)      … announcement.py codex '{…}'   (Codex)
#   … announcement.py essai            immediately plays the announcement to test the voice
#   … announcement.py robot < son.pcm  (UNO Q) sends 16 kHz mono 16-bit audio to the connected torso;
#                                exits with 0 if the transfer was received
# 
# No language model or key is involved: Piper, a separate GPL program, generates the fixed phrase
# once and caches it. Claude Code reports the end of every response, but only tasks lasting at
# least DUREE_MIN seconds are announced. The caller returns immediately while playback runs in
# the background, and the script always exits with 0.

import fcntl, glob, hashlib, json, os, re, select, subprocess, sys, termios, time
from datetime import datetime

ICI = os.path.dirname(os.path.abspath(__file__))
IA = os.environ.get("MOODIMOS_IA", os.path.expanduser("~/MoodiMOS-IA"))
PIPER = os.path.join(IA, "venv-cerveau", "bin", "piper")
VOIX = os.path.join(IA, "piper", "fr_FR-siwis-medium.onnx")
CACHE = os.path.join(IA, "annonces")
VOIX_ROBOT = os.environ.get("MOODIMOS_VOIX_ANNONCE", "robot doux")   # brain/robot_voice.py
DUREE_MIN = float(os.environ.get("MOODIMOS_ANNONCE_MIN", "30"))       # seconds
PORTS = ["/dev/cu.usbmodem*", "/dev/serial/by-id/*Espressif*"]         # Torso on Mac, on UNO Q (Linux)
JOURNAL = os.path.join(CACHE, "journal.log")                            # Each task end: announcement or silence
# On the UNO Q, the conversation relay (`robot_conversation.py`, Mac service) manages the torso port: it releases it to the process writing its process number in PAUSE, and reclaims it when VERROU (flock) is free.
VERROU = os.path.expanduser("~/moodimos/port.verrou")
PAUSE = os.path.expanduser("~/moodimos/port.pause")
PHRASES = {
    "claude": "Claude a terminé le travail demandé{projet}.",
    "codex": "Codex a terminé le travail demandé{projet}.",
    "essai": "Claude a terminé le travail demandé{projet}.",
}


def noter(qui, decision, duree=None, projet=""):
    try:
        os.makedirs(CACHE, exist_ok=True)
        with open(JOURNAL, "a", encoding="utf-8") as f:
            f.write("{} {:6} {:8} {}{}\n".format(datetime.now().strftime("%Y-%m-%d %H:%M:%S"), qui, decision,
                    "" if duree is None else "{:.0f} s".format(duree), projet))
    except OSError:
        pass


def duree_derniere_demande(transcript):
    'Seconds since the last user message in the Claude Code thread (None if unknown).'
    dernier = None
    try:
        with open(transcript, encoding="utf-8", errors="replace") as f:
            for ligne in f:
                try:
                    d = json.loads(ligne)
                except ValueError:
                    continue
                if d.get("type") != "user" or d.get("isMeta"):
                    continue
                contenu = (d.get("message") or {}).get("content")
                tape = isinstance(contenu, str) or (isinstance(contenu, list) and any(
                    isinstance(c, dict) and c.get("type") == "text" for c in contenu))
                if tape and d.get("timestamp"):
                    dernier = d["timestamp"]
    except OSError:
        return None
    if not dernier:
        return None
    debut = datetime.fromisoformat(dernier.replace("Z", "+00:00")).timestamp()
    return time.time() - debut


def nom_projet(chemin):
    if not chemin:
        return ""
    nom = os.path.basename(os.path.normpath(chemin))
    return " dans le projet {}".format(nom.replace("MoodiMOS", "Moudi Moss")) if nom else ""


def son(texte):
    'Single-use WAV file of the robot voice phrase.'
    sys.path.insert(0, ICI)
    from robot_voice import FILTRES, robotiser
    os.makedirs(CACHE, exist_ok=True)
    cle = hashlib.sha1((VOIX_ROBOT + "|" + texte).encode()).hexdigest()[:16]
    final = os.path.join(CACHE, cle + ".wav")
    if not os.path.exists(final):
        brut = final + ".brut.wav"
        subprocess.run([PIPER, "-m", VOIX, "-f", brut], input=texte.encode(), check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        robotiser(brut, final, FILTRES.get(VOIX_ROBOT, "anull"))
        os.remove(brut)
    return final


def echanger(fd, octets, attente):
    "Writes to the serial port and reads the card's response after `attente` seconds."
    vue = memoryview(octets)
    while vue:
        vue = vue[os.write(fd, vue[:4096]):]
    rep, fin = b"", time.time() + attente
    while time.time() < fin and not re.search(rb"annonce[^\n]*\n", rep):   # Wait for the response line: "announce..."
        if select.select([fd], [], [], 0.05)[0]:
            rep += os.read(fd, 4096)
    return rep.decode("utf-8", "replace")


def attendre(fd, motif, delai):
    'Read until `motif` (bytes) is detected; return all read content.'
    rep, fin = b"", time.time() + delai
    while motif not in rep and time.time() < fin:
        if select.select([fd], [], [], 0.05)[0]:
            rep += os.read(fd, 4096)
    return rep


def envoyer_son(fd, pcm):
    '« announcement » followed by 4 KB audio chunks, each confirmed by a + from the board.'
    os.write(fd, b"annonce %d\n" % len(pcm))
    rep = attendre(fd, b"pret", 2.0)
    if b"pret" not in rep:
        return rep.decode("utf-8", "replace")
    for k in range(0, len(pcm), 4096):
        morceau = memoryview(pcm)[k:k + 4096]
        while morceau:
            morceau = morceau[os.write(fd, morceau):]
        if b"+" not in attendre(fd, b"+", 2.0):
            return 'No acknowledgement for chunk %d' % (k // 4096)
    return echanger(fd, b"", 3.0)


def pcm_de(wav):
    'Announcement sound for the torso: 16 kHz, mono, 16-bit.'
    pcm = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", wav, "-ac", "1", "-ar", "16000", "-f", "s16le", "-"],
                         capture_output=True, check=True).stdout
    return pcm[:len(pcm) & ~1]


def dire_par_le_robot(pcm):
    'Send the announcement to the USB-attached torso; confirm if it received it fully. The other cards (head) are only probed with "annonce 0", which the torso politely rejects and they remain unaware.'
    for port in sorted(p for motif in PORTS for p in glob.glob(motif)):
        try:
            fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
        except OSError:
            continue
        try:
            attr = termios.tcgetattr(fd)
            attr[0] = attr[1] = attr[3] = 0
            attr[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            termios.tcsetattr(fd, termios.TCSANOW, attr)
            termios.tcflush(fd, termios.TCIFLUSH)
            if "annonce refus" not in echanger(fd, b"annonce 0\n", 1.0):
                continue                                   # torso (or old firmware)
            rep = envoyer_son(fd, pcm)
            if "annonce : " in rep and "somme %d" % (sum(pcm) & 0xFFFFFFFF) in rep:
                return True
            noter("robot", "erreur", None, " " + rep.strip().replace("\n", " | ")[:120])
        except OSError:
            pass
        finally:
            os.close(fd)
    return False


def prendre_port(attente=10.0):
    'Torso port (only for this process, lock released; false if still engaged).'
    try:
        verrou = os.open(VERROU, os.O_CREAT | os.O_RDWR, 0o644)
    except OSError:
        return True                                      # No directory `~/moodimos` exists: no other user.
    fin = time.time() + attente
    while True:
        try:
            fcntl.flock(verrou, fcntl.LOCK_EX | fcntl.LOCK_NB)
            break
        except BlockingIOError:
            if time.time() > fin:
                return False
            with open(PAUSE, "w") as f:
                f.write(str(os.getpid()))
            time.sleep(0.1)
    try:
        with open(PAUSE) as f:
            mien = f.read().strip() == str(os.getpid())
        if mien:
            os.remove(PAUSE)
    except OSError:
        pass
    return True


def uno_q():
    'The UNO Q relay: MOODIMOS_UNO_Q, otherwise the one retained by `mac_service.sh`. Empty: no relay.'
    if "MOODIMOS_UNO_Q" in os.environ:
        return os.environ["MOODIMOS_UNO_Q"]
    plist = os.path.expanduser("~/Library/LaunchAgents/fr.moodimos.conversation.plist")
    try:
        r = subprocess.run(["plutil", "-extract", "EnvironmentVariables.MOODIMOS_UNO_Q", "raw", plist],
                           capture_output=True, text=True, timeout=5)
    except (OSError, subprocess.TimeoutExpired):
        return ""
    return r.stdout.strip() if r.returncode == 0 else ""


def dire_par_la_uno_q(pcm):
    'Connected robot to the UNO Q hub: copy this script, then feed it sound in "robot" mode.'
    hote = uno_q()
    if not hote:
        return False
    ssh = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=3", hote]
    try:
        with open(os.path.abspath(__file__), "rb") as f:
            copie = subprocess.run(ssh + ["mkdir -p moodimos && cat > moodimos/announcement.py"], stdin=f,
                                   capture_output=True, timeout=10)
        if copie.returncode != 0:
            return False
        dit = subprocess.run(ssh + ["python3 moodimos/announcement.py robot"], input=pcm, capture_output=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return False
    if dit.returncode != 0:
        noter("uno-q", "erreur", None, " " + dit.stdout.decode("utf-8", "replace").strip()[:120])
    return dit.returncode == 0


def annoncer(qui, projet):
    wav = son(PHRASES[qui].format(projet=projet))
    if os.environ.get("MOODIMOS_ANNONCE_ROBOT", "1") == "1":
        pcm = pcm_de(wav)
        if prendre_port() and dire_par_le_robot(pcm):   # Robot connected to this Mac: the conversation uses the port.
            noter(qui, "robot")
            return
        if dire_par_la_uno_q(pcm):
            noter(qui, "uno-q")
            return
    subprocess.run(["afplay", wav])


def main():
    qui = sys.argv[1] if len(sys.argv) > 1 else "essai"
    if qui == "robot":                                   # On UNO Q board: sound arrives from Mac via standard input.
        pcm = sys.stdin.buffer.read()
        sys.exit(0 if prendre_port() and dire_par_le_robot(pcm) else 1)
    if qui == "parler":                                  # detached process: it is this that assembles and speaks the phrase
        annoncer(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "")
        return
    projet = ""
    if qui == "claude":
        try:
            donnees = json.load(sys.stdin)
        except ValueError:
            donnees = {}
        if donnees.get("stop_hook_active"):              # relay via another hook: already announced
            return
        duree = duree_derniere_demande(donnees.get("transcript_path", ""))
        if duree is None or duree < DUREE_MIN:            # Unclear duration: silence preferred over incorrect speech.
            noter(qui, "silence", duree)
            return
        projet = nom_projet(donnees.get("cwd"))
        noter(qui, "annonce", duree, projet)
    elif qui == "codex":
        try:
            donnees = json.loads(sys.argv[-1]) if len(sys.argv) > 2 else {}
        except ValueError:
            donnees = {}
        if donnees.get("type") not in (None, "agent-turn-complete"):
            return
        projet = nom_projet(donnees.get("cwd"))
        noter(qui, "annonce", None, projet)
    elif qui == "essai":
        annoncer("essai", nom_projet(os.getcwd()))
        return
    else:
        return
    subprocess.Popen([sys.executable, os.path.abspath(__file__), "parler", qui, projet],
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                     start_new_session=True)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        pass
    sys.exit(0)
