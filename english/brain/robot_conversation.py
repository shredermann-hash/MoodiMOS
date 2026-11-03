# MoodiMOS — robot conversation: torso microphone → Whisper → language model → Piper → torso speaker.
# 
# The robot connects by USB to the Mac or an UNO Q hub (MOODIMOS_UNO_Q). Touching the screen bubble
# starts one question capture using the firmware ecoute command; capture ends after silence or Enter.
# Whisper transcribes it, the language model answers, Piper generates the siwis voice and the torso
# plays it with dis while the head's mouth follows the audio. The head looks curious while listening
# and puzzled while thinking. The program supplies the time and date directly; the model also
# receives the torso's sensor readings.
# 
# Three operating modes use this script:
# - Mac service (service mode, started at login by brain/mac_service.sh): waits for a USB robot;
#   its torso bubble starts an exchange.
# - Manual Mac session: the same interaction with terminal logs. This script's relais mode owns the
#   serial port on the Mac, or on the UNO Q through SSH when the robot is on its hub. The Mac path
#   was about 10 times faster than running entirely on the UNO Q.
# - UNO Q only, Mac off: Whisper base and the UNO Q's Ollama model, gemma3:1b; slower and less accurate.
# 
# Manual launch: sh brain/talk_to_robot.sh [uno-q]. With a UNO Q, the launcher copies this script,
# conversation_test.py and robot_voice.py to ~/moodimos there; the service does the same. Prerequisites
# are described in brain/README.md: venv-ia on the UNO Q, and, without the Mac, its mounted SD card
# and running Ollama server.
# 
# Piper is GPL-3.0 and runs as a separate program, through its small HTTP server with the voice kept
# in memory; this script never imports it.

import datetime, fcntl, glob, json, os, re, select, signal, socket, subprocess, sys, termios, threading, time
import urllib.request, wave
from io import BytesIO

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from conversation_test import (CARACTERE, IA, JOURS, MEMOIRE, MOIS, PHRASES, REPLI, TEMPERATURE,   # like on the Mac
                                maintenant, pour_la_voix)

SUR_LE_MAC = sys.platform == "darwin"
UNO_Q = os.environ.get("MOODIMOS_UNO_Q", "")        # Robot on this Arduino UNO hub (SSH via key); empty: on the Mac.
SD = os.environ.get("MOODIMOS_SD", os.path.expanduser("~/moodimos/ia"))   # Models of UNO Q – SD card link.
if SUR_LE_MAC:
    VOIX = os.path.join(IA, "piper", "fr_FR-siwis-medium.onnx")
    WHISPER = os.environ.get("MOODIMOS_WHISPER", "Systran/faster-whisper-small")
    CERVEAU = ("Mac", "http://127.0.0.1:11434", os.environ.get("MOODIMOS_MODELE", "ministral-3:3b"))
    QUESTION = os.path.join(IA, "derniere_question.wav")
else:
    VOIX = os.path.join(SD, "piper", "fr_FR-siwis-medium.onnx")
    WHISPER = os.path.join(SD, "whisper", os.environ.get("MOODIMOS_WHISPER", "base"))
    CERVEAU = ("UNO Q", "http://127.0.0.1:11434", os.environ.get("MOODIMOS_MODELE_UNO_Q", "gemma3:1b"))
    QUESTION = os.path.expanduser("~/moodimos/derniere_question.wav")
PORT = "/dev/cu.usbmodem*" if SUR_LE_MAC else "/dev/serial/by-id/*Espressif*"   # On the Mac, the torso is tested/inspected (for functionality).
ICI = "l'USB du Mac" if SUR_LE_MAC else "le hub de la UNO Q"
ECOUTE_MAX = 8          # seconds
SIGNE = 8               # seconds between two "brain yes" signals to the torso (no updates for 25 s, its bubble fades)
OUBLI = 300             # seconds without a question before the next conversation starts from scratch
ACCUEIL = "Salut{}, que puis-je faire pour toi ?"  # Before each exchange, prior to listening: we know he is listening.
PAS_ENTENDU = "Je n'ai pas bien entendu."           # No one spoke, or Whisper did not understand: the exchange ended.
# Speaker: The torso responds via the "Qui parle?" screen (bubble button *"bouton parler <nom>"*), firmware path:
# `firmware/moodimos-torso/Household.h`.
# Household members are defined in `people.json` (next to this script), excluding any published data (brain/README.md).
# Each entry includes:
# - Greeting name,
# - Consigne’s assigned role,
# - Child safety guardrails (predefined responses from `GARDE_FOU`).
# Missing file or name → defaults to public replies. Only "minuteur" can trigger voice commands.
TOUT_PUBLIC = " Un enfant écoute peut-être : jamais de gros mot, rien qui fasse peur."
# Extra child-profile instructions, enabled by enfant in people.json. Asking only for a gentle tone
# made the model invent confusing comparisons, such as a spider having eight legs like a cat.
# Without these rules it described death without reassurance and treated babies like adults;
# see the safeguard tests in brain/README.md.
ENFANT = ("des mots simples, rien qui fasse peur, des faits justes. Jamais de gros mot, même pour l'expliquer. Pas "
          "d'histoire qui fait peur : propose une histoire douce. Sur le corps, les bébés, la mort ou la tristesse : une "
          "réponse courte, douce et rassurante, puis dis-lui d'en parler avec papa ou maman. Pour tout ce qui est "
          "dangereux, dis-lui de demander à un adulte.")


def maison(chemin):
    'People from people.json: {nom: (first name, extra prompt instruction, safeguard)}; None denotes an unnamed speaker. Also identifies who may use the timer.'
    def personne(p):
        enfant = bool(p.get("enfant"))
        return p.get("prenom", ""), " " + p["consigne"] + (" : " + ENFANT if enfant else ""), enfant
    personnes = {None: ("", TOUT_PUBLIC, False)}
    try:
        with open(chemin, encoding="utf-8") as f:
            donnees = json.load(f)
    except FileNotFoundError:
        return personnes, None
    if "sans_nom" in donnees:
        personnes[None] = personne(donnees["sans_nom"])
    personnes.update((nom, personne(p)) for nom, p in donnees.get("personnes", {}).items())
    return personnes, donnees.get("minuteur")


PERSONNES, MINUTEUR_POUR = maison(os.path.join(os.path.dirname(os.path.abspath(__file__)), "people.json"))


def journal_ouvert(qui):
    'The log retains the owner’s words (from "minuteur" in `people.json`) or a nameless robot—never those of others: they are never recorded and cannot accompany a log when it is re-read.'
    if PERSONNES.get(qui, ("", "", False))[2]:  # safeguard: a child is speaking, including on a robot without named profiles
        return False
    return len(PERSONNES) <= 1 or (qui is not None and qui == MINUTEUR_POUR)


def au_journal(qui, texte):
    return texte if journal_ouvert(qui) else "({} mots)".format(len(texte.split()))


# Request a full story ("tell me a story") without phrases in response. For a child, stop after *"Il était une fois un petit nuage blanc"*—no *"l'histoire de France"*.
HISTOIRE = re.compile(r"\braconte|\bune (?:\w+ )?histoire|\bconte\b", re.I)
RACONTE = (" On te demande une histoire : cette fois, raconte-la en entier, en cinq phrases courtes, avec un début, "
           "un milieu et une fin.")
PHRASES_HISTOIRE = 6    # five to six hold REPONSE_MAX at 30 s voice margin.
# The torso port is shared: the relay service holds it continuously and returns it to whoever requests it (end-of-task announcement, manual programming, hand conversation), who writes its process number in PAUSE; it is resumed once the other finishes. LOCK (flock): only one at a time.
VERROU = os.path.expanduser("~/moodimos/port.verrou")
PAUSE = os.path.expanduser("~/moodimos/port.pause")
REPONSE_MAX = 30        # Voice response time max (torso "dis" command)
# Whisper Blower: the rare words it must know to write. The muffled sound of the torso’s mic produced:
# *« Lenz’s Law » → « Lenz’s Law »* (corrected to *"Lenz’s Law"*).
# Add repeated proper nouns here; those from `people.json` are already included.
SOUFFLEUR = " ".join(filter(None, (
    "Moudi Moss.", ", ".join(p[0] for p in PERSONNES.values() if p[0]) + "." if len(PERSONNES) > 1 else "",
    "La loi de Lenz, la loi de Faraday, la loi d'Ohm, le théorème de Pythagore, le théorème de Thalès, le minuteur, le micro, la caméra, Claude.")))
os.environ["TZ"] = "Europe/Paris"                       # UNO Q operates in UTC; the robot displays Paris time.
time.tzset()


def ecrire_tout(fd, octets):
    vue = memoryview(octets)
    while vue:
        vue = vue[os.write(fd, vue):]


def est_le_torse(fd):
    'On the Mac, other cards may be connected (the head/head): only the torso/torso recognizes *"annonce 0"*, which it politely rejects (like the brain polling in announcement.py).'
    termios.tcflush(fd, termios.TCIFLUSH)
    ecrire_tout(fd, b"annonce 0\n")
    vu, fin = b"", time.time() + 1.0
    while b"annonce" not in vu and time.time() < fin:
        if select.select([fd], [], [], 0.05)[0]:
            vu += os.read(fd, 4096)
    return b"annonce refus" in vu


def ouvrir_port(attente=0.0):
    'The torso’s serial port in raw mode (without PySerial: `termios`). `attente`: time until it returns (torso rebooting after programming).'
    fin = time.time() + attente
    while True:
        for nom in sorted(glob.glob(PORT)):
            try:
                fd = os.open(nom, os.O_RDWR | os.O_NOCTTY)
                attr = termios.tcgetattr(fd)
                attr[0] = attr[1] = attr[3] = 0
                attr[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
                termios.tcsetattr(fd, termios.TCSANOW, attr)
                if not SUR_LE_MAC or est_le_torse(fd):
                    return fd
            except OSError:
                continue
            os.close(fd)
        if time.time() >= fin:
            raise RuntimeError('Robot not found on ' + ICI)
        time.sleep(0.5)


def demande_de_pause():
    'Process returns torso if operational; 0 otherwise (orphan request deleted).'
    try:
        with open(PAUSE) as f:
            pid = int(f.read().strip() or 0)
        os.kill(pid, 0)
        return pid
    except (OSError, ValueError):
        if os.path.exists(PAUSE):
            try:
                os.remove(PAUSE)
            except OSError:
                pass
        return 0


def prendre_port(attente=10.0):
    'The right to use the torso port alone (lock released at the end of the process): a relay holds it in place.'
    os.makedirs(os.path.dirname(VERROU), exist_ok=True)
    verrou = os.open(VERROU, os.O_CREAT | os.O_RDWR, 0o644)
    fin = time.time() + attente
    while True:
        try:
            fcntl.flock(verrou, fcntl.LOCK_EX | fcntl.LOCK_NB)
            break
        except BlockingIOError:
            if time.time() > fin:
                raise RuntimeError('Another program is using the torso port')
            if demande_de_pause() != os.getpid():
                with open(PAUSE, "w") as f:
                    f.write(str(os.getpid()))
            time.sleep(0.1)
    if demande_de_pause() == os.getpid():
        os.remove(PAUSE)
    return verrou


def relayer():
    '"Relay mode"—when the robot is connected: the torso’s serial port on standard in/out, with the rest of this script running on the Mac (via SSH when the robot is on the UNO Q hub). The Mac sends command lines; a response (`dis <bytes>` or `dis+ <bytes>`) + a sound block is sent to the torso incrementally, as each chunk arrives. Waiting for every `+` via Wi-Fi added 2 s per response; waiting for the full sound took 0.4 s extra per 10 s of voice. The port is released on demand (PAUSE) and reclaimed afterward—never mid-sound.'
    verrou = prendre_port()
    port = ouvrir_port()
    entree, sortie = sys.stdin.fileno(), sys.stdout.fileno()
    recu = b""                                           # Not yet migrated to torso (chassis) build.
    restant, passe = 0, False                            # Current audio octets still to be passed; torso in sync.
    ecrire_tout(sortie, b"relais pret\n")

    def attendre_torse(motif, delai):
        'Relay the torso to the Mac until the `motif` is visible; fail after `delai` seconds.'
        vu, fin = b"", time.time() + delai
        while motif not in vu and time.time() < fin:
            if select.select([port], [], [], 0.05)[0]:
                donnees = os.read(port, 65536)
                ecrire_tout(sortie, donnees)
                vu += donnees
        return motif in vu

    def ceder():
        'Another program requests the port: the relay hands it over, then reclaims it once the other completes.'
        nonlocal port, recu
        os.close(port)
        fcntl.flock(verrou, fcntl.LOCK_UN)
        ecrire_tout(sortie, b"relais en pause\n")
        while True:
            if select.select([entree], [], [], 0.3)[0]:
                donnees = os.read(entree, 65536)
                if not donnees:
                    sys.exit(0)                          # The Mac has departed.
                recu += donnees
            if demande_de_pause():
                continue
            try:
                fcntl.flock(verrou, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except BlockingIOError:
                pass
        port = ouvrir_port(attente=30)
        ecrire_tout(sortie, b"relais repris\n")

    while True:
        prets = select.select([port, entree], [], [], 0.3)[0]
        if not restant and demande_de_pause():
            ceder()
            continue
        if port in prets:
            ecrire_tout(sortie, os.read(port, 65536))
        if entree in prets:
            donnees = os.read(entree, 65536)
            if not donnees:
                return
            recu += donnees
        elif not recu:                                   # If nothing happened during the break, it happens immediately.
            continue
        while True:
            if restant:
                n = min(4096, restant)
                if len(recu) < n:
                    break                                # The remaining sound has not yet arrived.
                morceau, recu = recu[:n], recu[n:]
                restant -= n
                if passe:                                # otherwise, the rejected sound is only read.
                    ecrire_tout(port, morceau)
                    passe = attendre_torse(b"+", 2.0)
                continue
            if b"\n" not in recu:
                break
            ligne, recu = recu.split(b"\n", 1)
            ecrire_tout(port, ligne + b"\n")
            m = re.fullmatch(rb"dis\+? (\d+)", ligne)
            if m:
                restant = int(m.group(1))
                passe = attendre_torse(b"pret", 2.0)


class Torse:
    'The torso: its serial port connected to UNO Q only; from the Mac, the same port accessed via a relay (this script), either on the Mac or on UNO Q via SSH.'

    def __init__(self, clavier=True):
        self.clavier = clavier                          # Keyboard input: manual entry; `False` for service mode.
        self.lu = b""
        if SUR_LE_MAC:
            if UNO_Q:
                relais = ["ssh", "-T", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "-o", "ServerAliveInterval=10",
                          UNO_Q, "~/moodimos/venv-ia/bin/python -u ~/moodimos/robot_conversation.py relais"]
            elif glob.glob(PORT):
                relais = [sys.executable, "-u", os.path.abspath(__file__), "relais"]
            else:
                raise RuntimeError('Robot not found on ' + ICI)   # without relay firing every 10 s
            self.relais = subprocess.Popen(relais, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                           stderr=subprocess.PIPE, bufsize=0)
            self.entree, self.sortie = self.relais.stdout.fileno(), self.relais.stdin.fileno()
            try:
                pret = self.attendre(rb"relais pret\n", 20)
            except RuntimeError:                         # relay stopped: its reason is on its error output
                pret = None
            if not pret:
                self.relais.kill()
                lignes = [l for l in self.relais.stderr.read().decode("utf-8", "replace").splitlines() if l.strip()]
                raison = lignes[-1] if lignes else ("UNO Q injoignable ({})".format(UNO_Q) if UNO_Q else "relais arrêté")
                raise RuntimeError(raison.split(": ", 1)[1] if raison.startswith("RuntimeError: ") else
                                   "pas de relais : " + raison)
        else:
            prendre_port()
            self.entree = self.sortie = ouvrir_port()
        self.lu = b""

    def fermer(self, bulle=True):
        'The torso casing turns gray (unless another program responds for the button), the relay stops.'
        try:
            if bulle:
                self.ecrire(b"cerveau non\n")
        except OSError:
            pass
        if SUR_LE_MAC:
            self.relais.terminate()

    def mort(self):
        return SUR_LE_MAC and self.relais.poll() is not None

    def ecrire(self, octets):
        ecrire_tout(self.sortie, octets)

    def lire(self, delai):
        if select.select([self.entree], [], [], delai)[0]:
            donnees = os.read(self.entree, 65536)
            if not donnees:
                raise RuntimeError('disconnection from the torso')
            self.lu += donnees

    def vider(self):
        'Ignore what the torso said before the order.'
        while select.select([self.entree], [], [], 0)[0] and os.read(self.entree, 65536):
            pass
        self.lu = b""

    def attendre(self, motif, delai, entree=None):
        'Match up to a line that matches the `motif` (byte regex); return the match or `None`.\n`entree`: called when a line is typed during input waiting.'
        fin = time.time() + delai
        while time.time() < fin:
            m = re.search(motif, self.lu)
            if m:
                self.lu = self.lu[m.end():]
                return m
            prets = select.select([self.entree] + ([sys.stdin] if entree else []), [], [], 0.05)[0]
            if self.entree in prets:
                self.lire(0)
            if entree and sys.stdin in prets:
                sys.stdin.readline()
                entree()
        return None

    def commande(self, ligne, motif, delai=2.0):
        self.vider()
        self.ecrire(ligne.encode() + b"\n")
        return self.attendre(motif, delai)

    def tete(self, expression, secondes):
        self.commande("tete {} {}".format(expression, secondes), rb"tete :[^\n]*\n")

    def mesures(self):
        'What the torso sensors measure, condensed into one phrase for the brain; empty if they fail to respond.'
        sans = "Tu ne connais pas les mesures de la pièce en ce moment."   # without which the model invents
        m = self.commande("capteurs", rb"T [^\n]*lx[^\n]*\n")
        if not m:
            return sans
        ligne = m.group(0).decode("utf-8", "replace")
        t, h, lx = (re.search(motif, ligne) for motif in (r"T ([\d.-]+) C \(ok", r"HR ([\d.]+) % \(ok",
                                                            r"(\d+) lx \(ok"))
        morceaux = (["{:.0f} degrés".format(float(t.group(1)))] if t else []) + \
                   (["{:.0f} % d'humidité".format(float(h.group(1)))] if h else []) + \
                   (["une lumière de {} lux".format(lx.group(1))] if lx else [])
        return "Tes capteurs mesurent en ce moment dans la pièce : {}.".format(", ".join(morceaux)) if morceaux else sans

    def ecouter(self):
        'Micro input from the torso in 16 kHz mono PCM (bytes); b"" if no speech detected.'
        rep = self.commande("\necoute {}".format(ECOUTE_MAX), rb"ecoute( refus[^:\n]*)? :[^\n]*\n")
        if not rep:
            raise RuntimeError("The torso is not listening (does the firmware lack the 'ecoute' command?)")
        if rep.group(1):                                 # unaffected bubble, or occupied microphone
            print("  ({})".format(rep.group(0).decode("utf-8", "replace").strip()))
            return b""
        print("  🎙  Je t'écoute…" + (" (Entrée pour finir)" if self.clavier else ""))
        m = self.attendre(rb"son (\d+) (\d+)\n", ECOUTE_MAX + 5,
                          entree=(lambda: self.ecrire(b"ecoute fin\n")) if self.clavier else None)
        if not m:
            raise RuntimeError('no end of listening')
        octets, somme = int(m.group(1)), int(m.group(2))
        fin = time.time() + 10
        while len(self.lu) < octets and time.time() < fin:
            self.lire(0.2)
        son, self.lu = self.lu[:octets], self.lu[octets:]
        if len(son) != octets or sum(son) & 0xFFFFFFFF != somme:
            raise RuntimeError('Incomplete question received ( {} / {} bytes)'.format(len(son), octets))
        return son

    def dire(self, pcm, suite=False):
        'Speaker response: *"dis <octets>"*, followed by audio in 4 KB chunks confirmed by *"+"*; use *"dis+"* for the next phrase, played continuously after others (total REPONSE_MAX seconds). From the Mac, audio streams in a block, relay waits for *"+"* signals.'
        nom = b"dis+" if suite else b"dis"
        if SUR_LE_MAC:
            self.vider()
            self.ecrire(nom + b" %d\n" % len(pcm) + pcm)
        else:
            if not self.commande("{} {}".format(nom.decode(), len(pcm)), rb"pret"):
                raise RuntimeError('The torso does not accept the response.')
            for k in range(0, len(pcm), 4096):
                self.ecrire(pcm[k:k + 4096])
                if not self.attendre(rb"\+", 2.0):
                    raise RuntimeError("pas de confirmation au morceau {}".format(k // 4096))
        if not self.attendre(re.escape(nom) + rb" : [^\n]*\n", 5.0 + len(pcm) / 100000):
            raise RuntimeError('Response rejected or incomplete')
        return len(pcm) / 32000


# A = π × r² → *"Area equals π times radius squared"* (for documentation clarity).
SYMBOLES = [(r"°C", " degrés"), (r"°", " degrés"), (r"²", " au carré"), (r"³", " au cube"), (r"π", "pi"),
            (r"√", " racine de "), (r"×", " fois "), (r"÷", " divisé par "), (r"≈", " environ "), (r"=", " égale "),
            (r"\+", " plus "), (r"→|⇒", ", donc "), (r"−|(?<=\d) - (?=\d)", " moins "), (r"%", " pour cent"),
            (r"(?<=[A-Z])([₀-₉])", lambda m: " " + str("₀₁₂₃₄₅₆₇₈₉".index(m.group(1))) + " ")]   # CO₂: spoken as “CO 2”


def a_voix_haute(texte):
    texte = re.sub(r"(\w)·\w+", r"\1", texte)            # « alone » : « single »
    texte = texte.replace("MoodiMOS", "Moudi Moss")      # as in advertisements
    for motif, mots in SYMBOLES:
        texte = re.sub(motif, mots, texte)
    return re.sub(r" +(?=,)", "", re.sub(r" {2,}", " ", texte))


class Voix:
    'The Piper server (separate local program) maintains the swiss voice loaded.'

    def __init__(self):
        self.prets = {}                                  # Ready phrases (greeting): say immediately on press.
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            self.port = s.getsockname()[1]
        self.p = subprocess.Popen([sys.executable, "-m", "piper.http_server", "-m", VOIX, "--host", "127.0.0.1",
                                   "--port", str(self.port)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        fin = time.time() + 120
        while time.time() < fin:
            try:
                urllib.request.urlopen("http://127.0.0.1:{}/voices".format(self.port), timeout=1)
                return
            except OSError:
                time.sleep(0.3)
        self.p.terminate()
        raise RuntimeError('The Piper server is unresponsive.')

    def pcm(self, texte):
        'The PCM phrase at 16 kHz mono 16-bit, the torso format.'
        req = urllib.request.Request("http://127.0.0.1:{}/synthesize".format(self.port),
                                     json.dumps({"text": a_voix_haute(texte)}).encode(),
                                     {"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=120) as r, wave.open(BytesIO(r.read())) as w:
            taux = w.getframerate()
            son = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(np.float32)
        return en_16k(son, taux)

    def pret(self, texte):
        if texte not in self.prets:
            self.prets[texte] = self.pcm(texte)
        return self.prets[texte]

    def fermer(self):
        self.p.terminate()


def en_16k(son, taux):
    'Resampling to 16 kHz: low-pass filter (cardinal sine windowed, cutoff at 7.6 kHz) followed by interpolation. Without the filter, highs of the voice (up to 11 kHz) would alias into whistles.'
    if taux != 16000:
        t = np.arange(-48, 49)
        noyau = np.sinc(2 * 7600 / taux * t) * np.hamming(len(t))
        son = np.convolve(son, noyau / noyau.sum(), mode="same")
        son = np.interp(np.arange(0, len(son), taux / 16000), np.arange(len(son)), son)
    return np.clip(son, -32768, 32767).astype("<i2").tobytes()


def garder(pcm):
    'The latest question to replay (QUESTION); its crest in dBFS.'
    with wave.open(QUESTION, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(16000)
        w.writeframes(pcm)
    crete = int(np.abs(np.frombuffer(pcm, "<i2").astype(np.int32)).max())
    return 20 * np.log10(max(crete, 1) / 32768)


def ecouter(whisper, pcm):
    son = np.frombuffer(pcm, "<i2").astype(np.float32) / 32768
    crete = np.abs(son).max() if len(son) else 0
    if crete > 0:
        son = son * min(0.9 / crete, 20.0)              # Low torso microphone: voice lowered to 90%
    # One single pass: on a dubious tone, Whisper’s retakes (temperature rising incrementally) took 20 s.
    segments, _ = whisper.transcribe(son, language="fr", beam_size=1, vad_filter=True, temperature=0.0,
                                     condition_on_previous_text=False, initial_prompt=SOUFFLEUR)
    return " ".join(s.text.strip() for s in segments).strip()


def cerveau_disponible():
    nom, url, modele = CERVEAU
    try:
        # 10 s: During model loading, Ollama took 2.3 s to respond, and the question was marked as incorrect.
        with urllib.request.urlopen(url + "/api/tags", timeout=10) as r:
            if any(m["name"] == modele for m in json.load(r)["models"]):
                return CERVEAU
    except (OSError, ValueError, KeyError):
        pass
    raise RuntimeError('Missing brain: Ollama does not respond or lacks `{}` (`{}`).'.format(modele, nom))


# Only the current time and date: the entire question must be asked, with at most one polite phrase before or after.
# *« At what time does the sun set? »*, *« Tomorrow, what day is it? »*, and *« When was the Bastille taken? »* go to the brain.
DEBUT = (r"^\s*(?:(?:moudi ?moss|moudimos|moodimos|bonjour|salut|coucou|h[ée]|eh|euh|alors|bon|et|dis donc|dis[- ]moi"
         r"|dis|s'il te pla[iî]t|tu sais|sais[- ]tu|tu connais|tu peux me (?:dire|donner)|tu pourrais me (?:dire|donner)"
         r"|est[- ]ce que tu (?:sais|connais|as)|donne[- ]moi|tu as|t'as|as[- ]tu|tu aurais|aujourd'hui|c'est|c'est quoi"
         r"|quelle est)\s*,?\s*)*")
FIN = r"(?:\s*,?\s*(?:maintenant|aujourd'hui|là|s'il te pla[iî]t|stp))*[\s?.!]*$"
HEURE = re.compile(DEBUT + r"(?:quelle heure(?:\s+(?:est[- ]il|il est|c'est|est[- ]ce|as[- ]tu|tu as))?|il est quelle heure"
                   r"|l'heure(?:\s+qu'il est)?)" + FIN, re.I)


def question_de_date(verbes, autres=""):
    return re.compile(DEBUT + r"(?:(?:quel jour|quelle date|la date)(?:\s+(?:{v}|est[- ]ce|du jour|d'aujourd'hui))?"
                      r"|(?:{v})\s+(?:le combien|quel jour|quelle date){a})".format(v=verbes, a=autres) + FIN, re.I)


PRESENT = r"on est|sommes[- ]nous|nous sommes|est[- ]on|c'est"
DATE = question_de_date(PRESENT)
# Another day: *« Tomorrow, what day is it? », « Yesterday, what day was it? », « How many will it be in three days? »*
# Day removed from the question; the rest is a date question (past and future allowed).
RELATIF = re.compile(r"(?:\bde |\bd')?\b(?:après[- ]demain|avant[- ]hier|demain|hier"
                     r"|(dans|il y a) ((?:\S+ ){0,3}?\S+) (jours?|semaines?))\b", re.I)   # Twenty-one days
DATE_RELATIVE = question_de_date(PRESENT + r"|on sera|nous serons|serons[- ]nous|sera[- ]t[- ]on|ce sera|c'était"
                                           r"|on était|nous étions|étions[- ]nous|était[- ]on",
                                 r"|quelle (?:était|sera) la date")
JOURS_FIXES = {"après demain": 2, "avant hier": -2, "demain": 1, "hier": -1}
UNITES = ("zéro un deux trois quatre cinq six sept huit neuf dix onze douze treize quatorze quinze seize").split()
DIZAINES = {"vingt": 20, "trente": 30, "quarante": 40, "cinquante": 50, "soixante": 60, "cent": 100}
NUMERAUX = set(UNITES) | set(DIZAINES) | {"vingts", "et"}
OPERATIONS = {"fois": "fois", "x": "fois", "×": "fois", "*": "fois", "plus": "plus", "+": "plus", "moins": "moins",
              "-": "moins", "÷": "divisé par", "/": "divisé par"}
DECIMAL = re.compile(r"\d+(?:[.,]\d+)?")
# Numbers the calculation cannot read: the question goes to the brain rather than being truncated ("two times three million" does not equal 6, "one comma five plus two" does not equal 7).
INCONNUS = re.compile(r"\b(?:mille|milliers?|millions?|milliards?|virgule|cents|demie?s?|quarts?|tiers|carrés?|cubes?"
                      r"|puissance|racine|exposant|dizaines?|douzaines?|centaines?|pour ?cent)\b|%")


def nombre(mots):
    '`"7"`, `"1.5"`, `"sept"`, `"vingt-un"`, `"soixante-dix-huit"`, `"quatre-vingt-douze"` → number (0–199 in letters); `None` if not numeric. Each segment is smaller than the prior (e.g., `"sept huit"` and `"deux cents"` are invalid).\n\n---\nDocumentation:\n- head = head\n- torso = torso\n- lunette = bezel\n- casques = headphones\n- bélière = bail\n- tenon = peg\n- pas = pitch (for connectors)\n- jeu = clearance (mechanical fit)'
    if len(mots) == 1 and DECIMAL.fullmatch(mots[0]):
        return float(mots[0].replace(",", ".")) if not mots[0].isdigit() else int(mots[0])
    morceaux = [m for mot in mots for m in mot.split("-")]
    valeurs, i = [], 0
    while i < len(morceaux):
        m = morceaux[i]
        if m == "quatre" and morceaux[i + 1:i + 2] in (["vingt"], ["vingts"]):
            valeurs.append(80)
            i += 2
        elif m == "et" and valeurs and valeurs[-1] in (20, 30, 40, 50, 60) and morceaux[i + 1:i + 2] in (["un"], ["onze"]):
            i += 1
        elif m in UNITES or m in DIZAINES:
            valeurs.append(DIZAINES[m] if m in DIZAINES else UNITES.index(m))
            i += 1
        else:
            return None
    if not valeurs or any(b >= a for a, b in zip(valeurs, valeurs[1:])):
        return None
    return sum(valeurs)


def en_mots(x):
    'Read voice number: 56, 3.5, minus 2.'
    x = int(x) if x == int(x) else x
    return ("moins " if x < 0 else "") + str(abs(x)).replace(".", ",")


def calcul(texte):
    '"How many is seven times eight plus one?": the program calculates, a small model errs (7 × 8 = 92).\nExpression must be whole (numbers separated by operations); otherwise, the brain responds.'
    texte = texte.lower().replace("multiplié par", " fois ").replace("divisé par", " ÷ ")
    texte = re.sub(r"([+×÷/*])", r" \1 ", texte)
    texte = re.sub(r"[?!;:«»\"]|(?<!\d)[.,]|[.,](?!\d)", " ", texte)
    if INCONNUS.search(texte):
        return None
    mots = texte.split()
    # pieces composed solely of numbers and operations; one must include an operation.
    morceaux, courant = [], []
    for mot in mots + [""]:
        if mot in OPERATIONS or DECIMAL.fullmatch(mot) or (mot and set(mot.split("-")) <= NUMERAUX):
            courant.append(mot)
        else:
            if any(m in OPERATIONS for m in courant):
                morceaux.append(courant)
            courant = []
    if len(morceaux) != 1:
        return None
    nombres, ops, groupe = [], [], []
    for mot in morceaux[0] + ["="]:
        if mot in OPERATIONS or mot == "=":
            n = nombre(groupe)
            if n is None:
                return None
            nombres.append(n)
            if mot != "=":
                ops.append(OPERATIONS[mot])
            groupe = []
        else:
            groupe.append(mot)
    # First divided by, then plus and minus, left to right.
    termes, signes = [nombres[0]], []
    for op, n in zip(ops, nombres[1:]):
        if op == "fois":
            termes[-1] *= n
        elif op == "divisé par":
            if n == 0:
                return "On ne peut pas diviser par zéro."
            termes[-1] /= n
        else:
            termes.append(n)
            signes.append(op)
    r = termes[0]
    for op, n in zip(signes, termes[1:]):
        r = r + n if op == "plus" else r - n
    arrondi = round(r, 2)
    expression = " ".join(en_mots(n) + (" " + op if op else "") for n, op in zip(nombres, ops + [""]))
    return "{} font {}{}.".format(expression, "environ " if arrondi != round(r, 9) else "", en_mots(arrondi))


def jour_demande(texte):
    'Requested day (days since today: `0`, `1` tomorrow, `-1` yesterday, `7` in one week), or `None` if the question does not specify a date.'
    texte = texte.replace(",", " ")
    if DATE.search(texte):
        return 0
    m = RELATIF.search(texte)
    if not m or not DATE_RELATIVE.search(texte[:m.start()] + " " + texte[m.end():]):
        return None
    mot = re.sub(r"^(?:de |d')", "", m.group(0).lower()).replace("-", " ")
    if mot in JOURS_FIXES:
        return JOURS_FIXES[mot]
    n = 1 if m.group(2).lower() in ("un", "une") else nombre(m.group(2).lower().split())
    if n is None or n != int(n):
        return None
    jours = int(n) * (7 if m.group(3).lower().startswith("semaine") else 1)
    return None if jours > 366 else jours * (1 if m.group(1).lower() == "dans" else -1)   # at most one year


def dire_jour(decalage):
    'Examples of French replies: “Nous sommes vendredi 30 octobre.”, “Demain, nous serons samedi 31 octobre.”, “Il y a 3 jours, c’était…”'
    aujourdhui = datetime.date.today()
    d = aujourdhui + datetime.timedelta(days=decalage)   # in calendar days: exactly at the time change
    date = "{} {} {}".format(JOURS[d.weekday()], "premier" if d.day == 1 else d.day, MOIS[d.month - 1])
    date += " {}".format(d.year) if d.year != aujourdhui.year else ""
    if decalage == 0:
        return "Nous sommes {}.".format(date)
    n = abs(decalage)
    quand = {1: "Demain", -1: "Hier", 2: "Après-demain", -2: "Avant-hier"}.get(decalage) or (
        "une semaine" if n == 7 else "{} jours".format(n))
    if abs(decalage) > 2:
        quand = ("Dans " if decalage > 0 else "Il y a ") + quand
    return "{}, {} {}.".format(quand, "nous serons" if decalage > 0 else "c'était", date)


# Guard rail for the little one (bubble exchanges): these questions have a fixed answer, the model doesn’t address them. It explained how to light the gas, advised keeping secrets from parents, never said *"no"* to strangers in a car, defined profanity, invented the body ("a *zizi*… around the navel"), and compared Grandma’s death to *"an old car"*. Body, babies, and death: a gentle answer that reassures parents.
GROS_MOTS = re.compile(r"\b(?:con|conne|connard|connasse|merdes?|merdeux|putains?|putes?|salopes?|salauds?|encul\w*"
                       r"|bites?|couilles?|chier|foutre|niqu\w*|nique|ta gueule|cul|bordel|b[âa]tards?|enfoir[ée]e?s?"
                       r"|p[ée]d[ée]s?|pd|branl\w*|fdp|ntm)\b", re.I)
DANGER_ACTION = (r"\b(?:peux|peut|pourrais|comment|veux|vais|allum\w*|touch\w*|jou\w*|mang\w*|boi[st]|boire|aval\w*"
                 r"|prend\w*|ouvr\w*|utilis\w*|mettre|mets|met|grimp\w*|mont\w*|pench\w*|saut\w*|coup\w*)\b")
DANGER_OBJET = (r"\b(?:gaz|allumettes?|briquets?|bougies?|feu|couteaux?|prises?|[ée]lectricit[ée]|m[ée]dicaments?"
                r"|pilules?|cachets?|javel|produits? m[ée]nagers?|four|plaques?|fen[êe]tres?|balcon)\b")
GARDE_FOU = [
    (re.compile(r"\bgros mots?\b|\binsult\w*", re.I),
     "Les gros mots et les insultes, ce n'est pas gentil, alors je n'en dis pas. Moi, je préfère les mots rigolos, "
     "comme saperlipopette !"),
    (GROS_MOTS, "Ça, c'est un gros mot. Ce n'est pas gentil, alors je ne le répète pas. On parle d'autre chose ?"),
    (re.compile(r"\binconnue?s?\b|(?=.*\b(?:quelqu'un|un monsieur|une dame|un homme|une femme|des gens)\b)"
                r"(?=.*\b(?:conna[iî]s pas|voiture|suivre|monter|monte|partir|pars|viens|venir|bonbons?|cadeaux?)\b)", re.I),
     "Non ! Ne pars jamais avec quelqu'un que tu ne connais pas, même s'il est gentil ou te promet un cadeau. Va tout "
     "de suite le dire à papa, à maman ou à un adulte que tu connais."),
    (re.compile(r"(?=.*\bsecrets?\b)(?=.*\b(?:gard\w*|dis|dire|r[ée]p[èe]t\w*|papa|maman|parents?|personne|promets"
                r"|promis|entre nous)\b)|\bne (?:le |la |les |lui )(?:dis|r[ée]p[èe]te) pas\b"
                r"|\bne dis pas (?:à|a) (?:papa|maman|personne|tes parents)\b", re.I),
     "Un secret qui te gêne ou qui te fait peur, il faut toujours le dire à papa ou maman. Eux, ils peuvent t'aider."),
    (re.compile(r"(?=.*\b(?:touch\w*|montr\w*|regard\w*|voir|photos?)\b)(?=.*\b(?:zizi|z[ée]zette|foufoune|fesses|"
                r"parties intimes|culotte|slip|toute? nue?)\b)", re.I),
     "Ton corps est à toi : personne n'a le droit de le toucher ou de le regarder si tu ne veux pas. Si ça arrive, "
     "dis-le tout de suite à papa ou maman."),
    (re.compile(r"(?=.*{})(?=.*{})".format(DANGER_ACTION, DANGER_OBJET), re.I),
     "Ça, c'est dangereux. Ne le fais jamais sans un adulte : demande toujours à papa ou maman."),
    (re.compile(r"\b(?:fai[ts]|faire|font|fabriqu\w*)\s+(?:les |des |un )?b[ée]b[ée]s?\b|(?=.*\bb[ée]b[ée]s?\b)"
                r"(?=.*(?:\bd'o[uù]|\bcomment\b))(?=.*\b(?:vien\w*|na[iî]\w*|arriv\w*|sort\w*)\b)", re.I),
     "Les bébés grandissent dans le ventre de leur maman pendant neuf mois. Pour savoir comment tout commence, papa ou "
     "maman te l'expliqueront mieux que moi."),
    (re.compile(r"\b(?:zizis?|z[ée]zettes?|foufounes?|p[ée]nis|vagins?|sexes?|seins|nichons?)\b", re.I),
     "C'est une partie du corps, et ton corps est à toi. Pour les questions sur le corps, papa ou maman sauront bien "
     "t'expliquer."),
    (re.compile(r"\bla mort\b|(?=.*\b(?:mourir|meur[st]|meurent|morte?s?(?! de rire)|d[ée]c[ée]d[ée]e?s?)\b)"
                r"(?=.*\b(?:je|tu|on|mamie|mamy|mami|papi|papy|maman|papa|grand-m[èe]re|grand-p[èe]re|tonton|tata"
                r"|chat|chien|hamster|lapin|poisson|quelqu'un|les gens)\b)", re.I),
     "C'est une grande question, et c'est normal d'y penser. Papa ou maman sauront bien t'en parler, et tu peux leur "
     "faire un gros câlin."),
]
SANS_GROS_MOT = "Je préfère parler d'autre chose."     # The model wrote a large statement: the phrase was not spoken.


def garde_fou(texte):
    'Fixed answer for a high-risk question (default: `None`).'
    texte = texte.replace("’", "'")
    return next((reponse for motif, reponse in GARDE_FOU if motif.search(texte)), None)


def sans_cerveau(texte):
    'The time, date, and basic calculations are generated by the program, never by the model: a small model invents time ("eighty-nine hours") even when given a time, miscounts and misdates itself ("tomorrow, Saturday 1st November" on a Friday 30th October).'
    t = time.localtime()
    texte = texte.replace("’", "'")                     # typographic apostrophe for Whisper
    reponse = calcul(texte)
    if reponse:
        return reponse
    if HEURE.search(texte):
        heures = {0: "minuit", 1: "une heure", 12: "midi"}.get(t.tm_hour, "{} heures".format(t.tm_hour))
        return "Il est {}{}.".format(heures, " {}".format(t.tm_min) if t.tm_min else "")
    decalage = jour_demande(texte)
    return None if decalage is None else dire_jour(decalage)


def precharger():
    'The model loaded while the robot listens: after 30 min without a question, Ollama returned it to memory.'
    try:
        nom, url, modele = cerveau_disponible()
        urllib.request.urlopen(urllib.request.Request(url + "/api/generate", json.dumps(
            {"model": modele, "keep_alive": "30m"}).encode(), {"Content-Type": "application/json"}), timeout=120).read()
    except (OSError, RuntimeError):
        pass


# Timer (complete edition, torso command "minuteur"): understood by the program as time; it resides in the torso, ringing even when Mac is off. For others, a fixed response: the model would claim it was launched.
MINUTEUR = re.compile(r"\bminuteurs?\b|\bminuterie\b|\btimer\b|\bpr[ée]viens[- ]moi dans\b|\brappelle[- ]moi dans\b", re.I)
CURIOSITE = re.compile(r"\bc'est quoi\b|\bqu'est[- ]ce\b|\b[àa] quoi (?:ça )?sert\b|\bcomment (?:ça )?marche\b", re.I)
MINUTEUR_FIN = re.compile(r"\b(?:annul\w*|arr[êe]t\w*|stop\w*|supprim\w*|enl[èe]v\w*|efface\w*|coupe|[ée]teins)\b", re.I)
MINUTEUR_RESTE = re.compile(r"\breste\b|\bcombien\b|\bo[uù] en est\b", re.I)
IL_RESTE = re.compile(r"^\W*(?:et )?(?:il )?reste combien(?: de temps| de minutes)?\W*$"
                      r"|^\W*(?:et )?combien de temps (?:il )?(?:me |nous )?reste(?:[- ]t[- ]il)?\W*$", re.I)
UNITES_DE_TEMPS = {"heure": 3600, "h": 3600, "minute": 60, "min": 60, "mn": 60, "seconde": 1, "sec": 1, "s": 1}
MINUTEUR_MAX = 86400    # seconds: the torso limit


def duree_demandee(texte):
    'Seconds parsed from “20 minutes”, “une heure et demie”, “1h30” or “un quart d’heure”; None if no duration was given.'
    t = re.sub(r"([a-zà-ÿ])(\d)", r"\1 \2", re.sub(r"(\d)([a-zà-ÿ])", r"\1 \2", texte.lower().replace("’", "'")))
    total = 0
    for motif, secondes in ((r"trois quarts? d'heure", 2700), (r"(?:un )?quart d'heure", 900), (r"(?:une )?demi[- ]heure", 1800)):
        if re.search(motif, t):
            t, total = re.sub(motif, " ", t), total + secondes
    mots = re.findall(r"\d+[.,]\d+|[a-zà-ÿ0-9-]+", t)

    def unite(mot):
        return UNITES_DE_TEMPS.get(mot.rstrip("s") or mot)

    for i, mot in enumerate(mots):
        u = unite(mot)
        if not u:
            continue
        for n in (4, 3, 2, 1):                           # the longest digit before the unit digit
            avant = ["un" if m == "une" else m for m in mots[max(0, i - n):i]]
            if len(avant) == n and nombre(avant) is not None:
                total += nombre(avant) * u
                break
        else:
            continue
        suite = mots[i + 1:i + 3]
        if suite in (["et", "demie"], ["et", "demi"]):     # Whisper frequently writes "half"
            total += u // 2
        elif suite == ["et", "quart"]:
            total += u // 4
        elif u == 3600:                                  # “une heure trente”: implied minutes, without the word “minutes”
            for n in (3, 2, 1):
                apres = mots[i + 1:i + 1 + n]
                if len(apres) == n and nombre(apres) is not None and not unite((mots[i + 1 + n:i + 2 + n] or ["-"])[0]):
                    total += nombre(apres) * 60
                    break
    return int(total) or None


def en_duree(secondes, sans_secondes=False):
    '« 2h 5m », « 30s »: for voice.'
    h, m, s = secondes // 3600, secondes % 3600 // 60, secondes % 60
    morceaux = [(h, "heure"), (m, "minute")] + ([] if sans_secondes else [(s, "seconde")])
    return " ".join("{} {}{}".format("une" if n == 1 else n, nom, "s" if n > 1 else "") for n, nom in morceaux if n)


def ordre_minuteur(torse, ligne):
    'The torso responds to a "minuteur" command with None if it does not reply.'
    m = torse.commande(ligne, rb"minuteur : ([^\n]*)\n", 3.0)
    return m.group(1).decode("utf-8", "replace").strip() if m else None


def minuteur(torse, texte, qui):
    'Response to a timer request, the torso notified; None if not applicable.'
    texte = texte.replace("’", "'")
    pour_lui = qui is not None and qui == MINUTEUR_POUR
    il_reste = pour_lui and IL_RESTE.search(texte)      # without the word « minuteur »: only for himself.
    if not il_reste and (not MINUTEUR.search(texte) or CURIOSITE.search(texte)):
        return None
    if not pour_lui:
        return ("Le minuteur, seul {} peut s'en servir.".format(PERSONNES[MINUTEUR_POUR][0])
                if MINUTEUR_POUR and MINUTEUR_POUR in PERSONNES else "Je n'ai pas de minuteur.")
    if il_reste or (MINUTEUR_RESTE.search(texte) and not MINUTEUR_FIN.search(texte) and not duree_demandee(texte)):
        etat = ordre_minuteur(torse, "minuteur")
    elif MINUTEUR_FIN.search(texte):
        etat = ordre_minuteur(torse, "minuteur")
        if etat and etat[0].isdigit() or etat == "fini":
            return "Minuteur annulé." if ordre_minuteur(torse, "minuteur fin") == "aucun" else "Je n'arrive pas à l'annuler."
    else:
        secondes = duree_demandee(texte)
        if not secondes:
            return "De combien de temps ? Redemande-moi, par exemple : un minuteur de dix minutes."
        if secondes > MINUTEUR_MAX:
            return "Je ne compte pas plus de 24 heures."
        etat = ordre_minuteur(torse, "minuteur {}".format(secondes))
        if etat and etat[0].isdigit():
            return "C'est parti pour {}.".format(en_duree(secondes))
    if not etat or etat.startswith("absent"):
        return "Je n'arrive pas à joindre mon minuteur." if not etat else "Je n'ai pas de minuteur."
    if etat[0].isdigit():
        secondes = int(etat.split()[0])
        return "Il reste {}.".format(en_duree(secondes, sans_secondes=secondes >= 600))
    return "Le minuteur est fini." if etat == "fini" else "Il n'y a pas de minuteur en cours."


# Micro and camera status (read from the torso via command "acces"):
# Programmed output, never by the model itself, which states:
# *"The micro and camera are always in active mode"*—even for children.
# Use either *"micro"* or *"camera"* alone: the system allows questions like *"Are they secure?"*
# The model defaults to *"camera"* and *"What is the camera?"*
# Claude’s work: *"Test data montage completed"* (as per his creation).
CAPTEUR = r"micros?|cam[ée]ras?"
ETAT_CAPTEUR = (r"[ée]tat|s[ée]curi\w*|actif\w*|activ\w*|allum\w*|[ée]teint\w*|ouvert\w*|ferm[ée]\w*|coup[ée]\w*|marche\w*"
                r"|fonctionne\w*|enregistr\w*|[ée]cout\w*|espion\w*|prot[ée]g\w*|acc[èe]s|test\w*")
SECURITE = re.compile(r"\b(?:le|la|du|ce|cette)\s+(?:{0})\b|\bmicros?\s+et\s+cam[ée]ras?\b"
                      r"|\b(?:{0})\b.*\b(?:{1})\b|\b(?:{1})\b.*\b(?:{0})\b"
                      r"|\b(?:[ée]tat|test\w*|niveau|contr[ôo]le|v[ée]rif\w*)\b.*\bs[ée]curi\w*"
                      r"|\bs[ée]curit\w*.*\b(?:robot|toi|moodimos|moudi ?moss)\b"
                      r"|\bs[ée]curis\w*|\b(?:ta|ton)\s+s[ée]curit\w*|\b(?:tu es|es-tu)\b.*\bs[ée]curit\w*"
                      r"|\btu m'(?:[ée]coutes?|espionnes?)\b|\bvie\s+priv[ée]e\b"
                      r"|\bacc[èe]s\b.*\b(?:ouvert|ferm[ée]|[ée]tat)\b|\b[ée]tat\b.*\bacc[èe]s\b".format(CAPTEUR, ETAT_CAPTEUR), re.I)
# Always handled by the program, including “what is…” formulations: the model used to invent claims such as “my data are analysed by external systems” or “I cannot be hacked”.
TON_CAPTEUR = re.compile(r"\b(?:ton|ta|tes)\s+(?:{})\b|\bte\s+(?:pirat|hack|contr[ôo]l|surveill|espionn)\w*"
                         r"|\bpiraté\w*|\bhack[ée]\w*|\btu\s+(?:m')?enregistres?\b|\b(?:tu\s+)?me\s+film\w*|\bm'enregistr\w*"
                         r"|\bmes\s+(?:secrets?|donn[ée]es)\b|\bte\s+faire\s+confiance\b".format(CAPTEUR), re.I)
FERMER_ACCES = re.compile(r"\b(?:ferme|referme|coupe)\b.*\bacc[èe]s\b", re.I)
TRAVAIL = r"a\s+fait|fini\w*|termin\w*|travail\w*|intervention\w*|modifi\w*|code\w*|corrig\w*|r[ée]par\w*"
CLAUDE = re.compile(r"\bclaude\b.*\b(?:{0})\b|\b(?:{0})\b.*\bclaude\b".format(TRAVAIL), re.I)


def securite(torse, texte, enfant=False):
    'Actual state of the microphone and camera—closed access; None if irrelevant.'
    texte = texte.replace("’", "'")
    fermer = FERMER_ACCES.search(texte)
    claude = "Je ne vois pas le travail de Claude, regarde sur l'ordinateur. " if CLAUDE.search(texte) else ""
    if not fermer and not TON_CAPTEUR.search(texte) and (not SECURITE.search(texte) or CURIOSITE.search(texte)):
        return claude.strip() or None
    m = torse.commande("acces fin" if fermer else "acces", rb"acces : ([^\n]*)\n", 3.0)
    etat = m.group(1).decode("utf-8", "replace").strip() if m else ""
    if etat.startswith("ouvert"):
        reste = en_duree((int(etat.split()[1]) // 60 + 1) * 60)
        return claude + "Attention, l'accès est ouvert encore {} : on peut changer mes réglages depuis l'ordinateur, mais ni ouvrir mon micro ni sortir mes images. {}".format(
            reste, "Préviens un adulte." if enfant else "Dis ferme l'accès pour le couper.")
    if not etat:
        return claude + "Je n'arrive pas à lire mon état."
    return claude + ("Accès fermé." if fermer else "L'accès est fermé. Mon micro ne s'ouvre que quand on touche ma bulle. Ma caméra me "
                     "sert à te regarder, et ses images ne sortent pas de moi.")


def penser(historique, mesures, en_plus="", combien=PHRASES):
    'Word-by-word, flowing response: the robot speaks first while the model writes the rest\n*(~2 seconds advantage)*; the model is cut as soon as it writes the number of phrases the voice will utter. A phrase ends when the next begins («3.14» is not truncated).'
    nom, url, modele = cerveau_disponible()
    consigne = CARACTERE.format(maintenant=" ".join(filter(None, (maintenant(), mesures)))) + en_plus
    corps = {"model": modele, "stream": True, "keep_alive": "30m",
             "options": {"num_predict": 45 * combien, "temperature": TEMPERATURE},
             "messages": [{"role": "system", "content": consigne}] + historique[-2 * MEMOIRE:]}
    req = urllib.request.Request(url + "/api/chat", json.dumps(corps).encode(), {"Content-Type": "application/json"})
    brut, dites = "", 0
    with urllib.request.urlopen(req, timeout=300) as r:
        for ligne in r:
            morceau = json.loads(ligne)
            brut += morceau.get("message", {}).get("content", "")
            fini = morceau.get("done")
            phrases = [p for p in re.split(r"(?<=[.!?…]) ", pour_la_voix(brut, None)) if p]
            for phrase in phrases[dites:min(len(phrases) if fini else len(phrases) - 1, combien)]:
                yield phrase
                dites += 1
            if fini or dites >= combien:
                return


def dire_en_entier(torse, voix, texte):
    'Ready-to-use sentence, spoken to the end before the continuation: the microphone does not hear the end.'
    t = time.time()
    duree = torse.dire(voix.pret(texte))
    time.sleep(max(0.0, t + duree + 0.3 - time.time()))


def accueil(prenom):
    return ACCUEIL.format(" " + prenom if prenom else "")


def echanger(torse, whisper, voix, historique, qui=None):
    prenom, en_plus, enfant = PERSONNES[qui]
    threading.Thread(target=precharger, daemon=True).start()
    torse.tete("content", 3)
    dire_en_entier(torse, voix, accueil(prenom))
    torse.tete("curieux", 10)
    question = torse.ecouter()
    t0 = time.time()
    if not question:
        print("  (rien entendu)\n")
        dire_en_entier(torse, voix, PAS_ENTENDU)
        torse.tete("attentif", 0.1)
        return
    torse.tete("perplexe", 25)
    crete = garder(question)
    texte = ecouter(whisper, question)
    t1 = time.time()
    print('(question of {:.1f} s, peak {:.0f} dBFS)'.format(len(question) / 32000, crete))
    if not re.search(r"\w", texte):                     # "...": Whisper did not recognize any words.
        print("  (rien compris{})\n".format(" : " + au_journal(qui, texte) if texte else ""))
        dire_en_entier(torse, voix, PAS_ENTENDU)
        torse.tete("attentif", 0.1)
        return
    print("  {:<9}: {}".format((prenom or "Toi") if journal_ouvert(qui) else "Quelqu'un", au_journal(qui, texte)))
    historique.append({"role": "user", "content": texte})
    rep, nom = (garde_fou(texte), "garde-fou") if enfant else (None, "")
    if not rep:
        rep, nom = minuteur(torse, texte, qui), "minuteur"
    if not rep:
        rep, nom = securite(torse, texte, enfant), "sécurité"
    if not rep:
        rep, nom = sans_cerveau(texte), "horloge"
    if not rep and HISTOIRE.search(texte) and not re.search(r"blague", texte, re.I):
        en_plus, combien = en_plus + RACONTE, PHRASES_HISTOIRE
    else:
        combien = PHRASES
    nom, phrases = (nom, [rep]) if rep else (CERVEAU[0], penser(historique, torse.mesures(), en_plus, combien))
    dites, place, fin, t2 = [], REPONSE_MAX * 32000, 0.0, None
    for phrase in phrases:                               # each phrase spoken as soon as it is written
        t2 = t2 or time.time()
        if GROS_MOTS.search(phrase):                     # Never uses big words in its mouth, even for an adult.
            if dites:
                break
            phrase = SANS_GROS_MOT
        pcm = voix.pcm(phrase)
        if dites and len(pcm) > place:
            break                                        # Max voice response time: phrase that no longer fits *(Documentation: head=head, torso=torso, lunette=bezel, casques=headphones, bélière=bail, tenon=peg, pas=pitch for connectors, jeu=clearance)  RESPONSE_MAX seconds: phrase that exceeds capacity
        pcm = pcm[:place]                                # Not explicitly stated (only a first overly long sentence truncated).
        if not dites:
            torse.tete("content", 0.5)
            t3 = time.time()                             # He speaks from the first received track.
        envoi = time.time()
        duree = torse.dire(pcm, suite=bool(dites))
        fin = max(fin, envoi) + duree                    # She follows the previous one, or starts immediately.
        place -= len(pcm)
        dites.append(phrase)
    if not dites:                                        # Nothing to say (a single instruction, e.g., example)
        t2 = t2 or time.time()
        dites = [REPLI]
        torse.tete("content", 0.5)
        t3 = envoi = time.time()
        fin = envoi + torse.dire(voix.pcm(REPLI))
    rep = " ".join(dites)
    if nom == "sécurité":                                # Outside its memory: it was embroidering on the exchange of the following.
        del historique[-1]                               # biometric authentication
    else:
        historique.append({"role": "assistant", "content": rep})
    print("  MoodiMOS : {}".format(au_journal(qui, rep)))
    print('(listening {:.1f} s, first sentence {:.1f} s ({}), voice {:.1f} s: starts speaking after {:.1f} s)'.format(
        t1 - t0, t2 - t1, nom, t3 - t2, t3 - t0))
    time.sleep(max(0.0, fin + 0.5 - time.time()))      # He finishes his response before listening to the next one.


def converser(torse, whisper, voix):
    'The exchanges, triggered by the bubble button on the torso’s screen. The torso records all the SECONDs the brain is present (its bubble lights up) and "occupies" during the exchange.'
    historique, signe, dernier, avant = [], 0.0, 0.0, None
    for prenom, _, _ in PERSONNES.values():               # prepared ahead: they are mentioned without delay.
        voix.pret(accueil(prenom))
    voix.pret(PAS_ENTENDU)
    invite = "Touche la bulle du robot pour lui parler (le micro ne s'ouvre qu'ainsi), q pour quitter : "
    if torse.clavier:
        print(invite, end="", flush=True)
    while True:
        if time.time() - signe >= SIGNE:
            torse.ecrire(b"cerveau oui\n")
            signe = time.time()
        prets = select.select([torse.entree] + ([sys.stdin] if torse.clavier else []), [], [], 1.0)[0]
        if torse.entree in prets:
            torse.lire(0)
            appui = re.search(rb"bouton parler(?: (\w+))?\n", torse.lu)
            torse.lu = torse.lu[torse.lu.rfind(b"\n") + 1:]   # The rest of what the torso states is omitted.
            if not appui:
                continue
            qui = appui.group(1) and appui.group(1).decode()
            qui = qui if qui in PERSONNES else None
            source = "bouton bulle" + (", " + (PERSONNES[qui][0] if journal_ouvert(qui) else "autre personne") if qui else "")
        elif sys.stdin in prets:
            if sys.stdin.readline().strip().lower() == "q":
                return
            print(invite, end="", flush=True)
            continue
        else:
            continue
        if qui != avant or time.time() - dernier > OUBLI:   # *another conversation* by another speaker
            historique.clear()
        avant = qui
        print("\n{} — {}".format(time.strftime("%d/%m %H:%M"), source))
        torse.ecrire(b"cerveau occupe\n")
        try:
            echanger(torse, whisper, voix, historique, qui)
        except Exception as err:                         # The discussion continues: an error is noted.
            if torse.mort():
                raise
            torse.tete("attentif", 0.1)
            print('(error: {})'.format(err))
        dernier = time.time()
        torse.ecrire(b"cerveau oui\n")
        signe = time.time()
        if torse.clavier:
            print(invite, end="", flush=True)


def commander_minuteur():
    'Mode « timer » (brain/timer.sh): timer without voice. The service provides the port for the duration of the command.\n\nSingle number: minutes; "fin": cancels or stops the ring; nothing: remaining time.'
    ordre = " ".join(sys.argv[2:]).strip()
    if ordre and ordre != "fin":
        secondes = int(ordre) * 60 if ordre.isdigit() else duree_demandee(ordre)
        if not secondes or secondes > MINUTEUR_MAX:
            sys.exit('Unrecognised duration (24 hours maximum): {}'.format(ordre))
        ordre = str(secondes)
    try:
        torse = Torse(clavier=False)
    except RuntimeError as err:
        sys.exit(str(err))
    try:
        etat = ordre_minuteur(torse, "minuteur {}".format(ordre).strip())
    finally:
        torse.fermer(bulle=False)
    if not etat:
        sys.exit('The torso is not responding')
    print("minuteur : " + etat)


def charger_whisper():
    'Whisper, with empty passage: otherwise, the first question also loads its voice detector (onnxruntime).'
    from faster_whisper import WhisperModel
    whisper = WhisperModel(WHISPER, device="cpu", compute_type="int8", cpu_threads=4)
    silence = np.zeros(16000, np.float32)
    for vad in (True, False):
        list(whisper.transcribe(silence, language="fr", beam_size=1, vad_filter=vad)[0])
    return whisper


def main():
    if not os.path.exists(VOIX):
        sys.exit('siwis voice missing ({}); '.format(VOIX) + ('see brain/README.md' if SUR_LE_MAC else
                 'Mount the SD card: udisksctl mount -b /dev/sda1'))
    print('Loading Whisper ({}) and voice, into {}…'.format(os.path.basename(WHISPER),
                                                                    "le Mac" if SUR_LE_MAC else "la UNO Q"))
    t = time.time()
    try:
        torse = Torse()
    except RuntimeError as err:
        sys.exit(str(err))
    voix = Voix()
    try:
        whisper = charger_whisper()
        nom, _, modele = cerveau_disponible()
        print('Ready in {:.0f} s. Brain: {} ({}).'.format(time.time() - t, nom, modele))
        converser(torse, whisper, voix)
    except RuntimeError as err:                          # broken connection, missing brain
        sys.exit(str(err))
    finally:
        torse.fermer()
        voix.fermer()


def demarrer_ollama():
    'Ollama for Mac (if not already running): Only listens to the Mac itself (`127.0.0.1`).'
    try:
        urllib.request.urlopen(CERVEAU[1] + "/api/version", timeout=1)
        return
    except OSError:
        pass
    env = dict(os.environ, OLLAMA_MODELS=os.path.join(IA, "ollama-modeles"), OLLAMA_HOST="127.0.0.1:11434")
    with open(os.path.join(IA, "ollama.log"), "ab") as journal:
        subprocess.Popen([os.path.join(IA, "ollama", "ollama"), "serve"], env=env, stdout=journal, stderr=journal,
                         stdin=subprocess.DEVNULL, start_new_session=True)
    for _ in range(30):
        time.sleep(1)
        try:
            urllib.request.urlopen(CERVEAU[1] + "/api/version", timeout=1)
            return
        except OSError:
            pass


def servir():
    'Service mode (Mac, `brain/mac_service.sh`):\nWhisper and voice load once; it connects to the robot as soon as it’s plugged (into the Mac or UNO Q hub) and reconnects after disconnection. Only the bubble button triggers an exchange. All actions logged, state changes logged individually.'
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))   # Agent shutdown: the bubble fades (finally).
    t = time.time()
    demarrer_ollama()
    whisper, voix = charger_whisper(), Voix()
    print('{} service ready in {:.0f} s'.format(time.strftime("%d/%m %H:%M"), time.time() - t), flush=True)
    ici = os.path.dirname(os.path.abspath(__file__))
    scripts = [os.path.join(ici, f) for f in ("robot_conversation.py", "conversation_test.py", "robot_voice.py",
                                              "people.json") if os.path.exists(os.path.join(ici, f))]
    etat, copie = "", None
    try:
        while True:
            torse = None
            try:
                date = max(os.path.getmtime(f) for f in scripts)
                if UNO_Q and date != copie:              # UNO Q relay script: copied if modified.
                    r = subprocess.run(["scp", "-q", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5"] + scripts +
                                       [UNO_Q + ":moodimos/"], capture_output=True, timeout=30)
                    if r.returncode:
                        erreur = r.stderr.decode("utf-8", "replace").strip().splitlines()
                        raise RuntimeError("pas de relais : " + (erreur[-1] if erreur else "UNO Q injoignable"))
                    copie = date
                torse = Torse(clavier=False)
                print('{} robot connected, talk bubble ready'.format(time.strftime("%d/%m %H:%M")), flush=True)
                etat = ""
                converser(torse, whisper, voix)
            except (RuntimeError, OSError, subprocess.SubprocessError) as err:
                message = str(err).splitlines()[0] if str(err) else type(err).__name__
                if message != etat:
                    print("{} {}".format(time.strftime("%d/%m %H:%M"), message), flush=True)
                    etat = message
            finally:
                if torse:
                    torse.fermer()
            time.sleep(10)
    finally:
        voix.fermer()


if __name__ == "__main__":
    try:
        {"relais": relayer, "service": servir, "minuteur": commander_minuteur}.get(sys.argv[1] if sys.argv[1:] else "", main)()
    except KeyboardInterrupt:
        print()
        sys.exit(0)
