# MoodiMOS — annonce de fin de tâche : « Claude a terminé le travail demandé »
#
# Appelé par Claude Code (crochet Stop, fin d'une réponse : données JSON sur l'entrée standard) et par Codex
# (réglage notify de ~/.codex/config.toml : données JSON en dernier argument). La phrase est dite par le robot
# (commande « annonce » du firmware : il parle et fait sa joie) quand son torse est branché en USB sur le Mac, ou sur
# le hub de la UNO Q : le Mac lui passe alors le son par SSH et cette même copie du script, lancée là-bas en mode
# « robot », le relaie au torse. Sinon, les haut-parleurs du Mac.
#
#   … annonce.py claude          (Claude Code)      … annonce.py codex '{…}'   (Codex)
#   … annonce.py essai            dit l'annonce tout de suite, pour écouter la voix
#   … annonce.py robot < son.pcm  (sur la UNO Q) son 16 kHz mono 16 bits vers le torse branché ; sort avec 0 s'il l'a reçu
#
# Pas d'IA ni de clé : la phrase est fixe, fabriquée une fois par Piper (programme séparé, GPL) puis gardée en cache.
# Claude Code signale la fin de chaque réponse : seules celles qui ont duré au moins DUREE_MIN secondes sont annoncées.
# Le script ne bloque jamais : il rend la main tout de suite et parle en arrière-plan ; il sort toujours avec 0.

import fcntl, glob, hashlib, json, os, re, select, subprocess, sys, termios, time
from datetime import datetime

ICI = os.path.dirname(os.path.abspath(__file__))
IA = os.environ.get("MOODIMOS_IA", os.path.expanduser("~/MoodiMOS-IA"))
PIPER = os.path.join(IA, "venv-cerveau", "bin", "piper")
VOIX = os.path.join(IA, "piper", "fr_FR-siwis-medium.onnx")
CACHE = os.path.join(IA, "annonces")
VOIX_ROBOT = os.environ.get("MOODIMOS_VOIX_ANNONCE", "robot doux")   # un nom de cerveau/voix_robot.py
DUREE_MIN = float(os.environ.get("MOODIMOS_ANNONCE_MIN", "30"))       # secondes
PORTS = ["/dev/cu.usbmodem*", "/dev/serial/by-id/*Espressif*"]         # torse sur le Mac, sur la UNO Q (Linux)
JOURNAL = os.path.join(CACHE, "journal.log")                            # chaque fin de tâche : annonce ou silence
# Sur la UNO Q, le relais de la conversation (conversation_robot.py, service du Mac) tient le port du torse : il le rend
# à qui écrit son numéro de processus dans PAUSE, et le reprend quand VERROU (flock) est libre.
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
    """Secondes depuis le dernier message tapé par l'utilisateur dans le fil de Claude Code (None si inconnu)."""
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
    """Fichier WAV de la phrase avec la voix de robot, fabriqué une seule fois."""
    sys.path.insert(0, ICI)
    from voix_robot import FILTRES, robotiser
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
    """Écrit sur le port série et rend ce que la carte répond pendant `attente` secondes."""
    vue = memoryview(octets)
    while vue:
        vue = vue[os.write(fd, vue[:4096]):]
    rep, fin = b"", time.time() + attente
    while time.time() < fin and not re.search(rb"annonce[^\n]*\n", rep):   # attend la ligne de réponse « annonce … »
        if select.select([fd], [], [], 0.05)[0]:
            rep += os.read(fd, 4096)
    return rep.decode("utf-8", "replace")


def attendre(fd, motif, delai):
    """Lit jusqu'à voir `motif` (octets) ; rend tout ce qui a été lu."""
    rep, fin = b"", time.time() + delai
    while motif not in rep and time.time() < fin:
        if select.select([fd], [], [], 0.05)[0]:
            rep += os.read(fd, 4096)
    return rep


def envoyer_son(fd, pcm):
    """« annonce <octets> », puis le son par morceaux de 4 Ko, chacun confirmé par un « + » de la carte."""
    os.write(fd, b"annonce %d\n" % len(pcm))
    rep = attendre(fd, b"pret", 2.0)
    if b"pret" not in rep:
        return rep.decode("utf-8", "replace")
    for k in range(0, len(pcm), 4096):
        morceau = memoryview(pcm)[k:k + 4096]
        while morceau:
            morceau = morceau[os.write(fd, morceau):]
        if b"+" not in attendre(fd, b"+", 2.0):
            return "pas de confirmation au morceau %d" % (k // 4096)
    return echanger(fd, b"", 3.0)


def pcm_de(wav):
    """Son de l'annonce pour le torse : 16 kHz, mono, 16 bits."""
    pcm = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", wav, "-ac", "1", "-ar", "16000", "-f", "s16le", "-"],
                         capture_output=True, check=True).stdout
    return pcm[:len(pcm) & ~1]


def dire_par_le_robot(pcm):
    """Envoie l'annonce au torse branché en USB ; vrai s'il l'a reçue entière. Les autres cartes (la tête) sont
    seulement sondées avec « annonce 0 », que le torse refuse poliment et qu'elles ne connaissent pas."""
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
                continue                                   # pas le torse, ou ancien firmware
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
    """Le port du torse pour ce seul processus (verrou rendu à sa fin) ; faux s'il reste pris."""
    try:
        verrou = os.open(VERROU, os.O_CREAT | os.O_RDWR, 0o644)
    except OSError:
        return True                                      # pas de dossier ~/moodimos : personne d'autre
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
    """La UNO Q qui relaie : MOODIMOS_UNO_Q, sinon celle que service_mac.sh a gardée. Vide : pas de relais."""
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
    """Robot branché sur le hub de la UNO Q : y copie ce script, puis lui passe le son en mode « robot »."""
    hote = uno_q()
    if not hote:
        return False
    ssh = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=3", hote]
    try:
        with open(os.path.abspath(__file__), "rb") as f:
            copie = subprocess.run(ssh + ["mkdir -p moodimos && cat > moodimos/annonce.py"], stdin=f,
                                   capture_output=True, timeout=10)
        if copie.returncode != 0:
            return False
        dit = subprocess.run(ssh + ["python3 moodimos/annonce.py robot"], input=pcm, capture_output=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return False
    if dit.returncode != 0:
        noter("uno-q", "erreur", None, " " + dit.stdout.decode("utf-8", "replace").strip()[:120])
    return dit.returncode == 0


def annoncer(qui, projet):
    wav = son(PHRASES[qui].format(projet=projet))
    if os.environ.get("MOODIMOS_ANNONCE_ROBOT", "1") == "1":
        pcm = pcm_de(wav)
        if prendre_port() and dire_par_le_robot(pcm):   # robot branché sur ce Mac : la conversation prête le port
            noter(qui, "robot")
            return
        if dire_par_la_uno_q(pcm):
            noter(qui, "uno-q")
            return
    subprocess.run(["afplay", wav])


def main():
    qui = sys.argv[1] if len(sys.argv) > 1 else "essai"
    if qui == "robot":                                   # sur la UNO Q : le son arrive du Mac par l'entrée standard
        pcm = sys.stdin.buffer.read()
        sys.exit(0 if prendre_port() and dire_par_le_robot(pcm) else 1)
    if qui == "parler":                                  # processus détaché : c'est lui qui fabrique et dit la phrase
        annoncer(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "")
        return
    projet = ""
    if qui == "claude":
        try:
            donnees = json.load(sys.stdin)
        except ValueError:
            donnees = {}
        if donnees.get("stop_hook_active"):              # relance par un autre crochet : déjà annoncé
            return
        duree = duree_derniere_demande(donnees.get("transcript_path", ""))
        if duree is None or duree < DUREE_MIN:            # durée inconnue : on se tait plutôt que de parler à tort
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
