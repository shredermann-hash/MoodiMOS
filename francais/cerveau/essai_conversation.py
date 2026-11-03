# MoodiMOS — essai du cerveau local sans le robot : micro et haut-parleurs du Mac
#
# Chaîne complète d'une question : enregistrement au micro → Whisper (voix → texte) → modèle local d'Ollama
# (texte → réponse) → Piper, voix siwis (réponse → voix) → lecture. Le robot fera la même chose plus tard, le
# Mac jouant alors le rôle de « cerveau » par le Wi-Fi.
#
# Prérequis (voir cerveau/LISEZMOI.md) : serveur Ollama lancé, modèle tiré, environnement Python avec faster-whisper et
# piper-tts, voix siwis téléchargée. Lancer :
#   ~/MoodiMOS-IA/venv-cerveau/bin/python cerveau/essai_conversation.py
# Entrée pour commencer à parler, Entrée pour finir ; « v » pour changer de voix, « q » pour quitter.
#
# Piper est sous GPL-3.0 : il tourne comme programme séparé (son propre petit serveur, voix gardée en mémoire),
# jamais importé (même règle que R3B00T).

import json, os, re, socket, subprocess, sys, tempfile, time, urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from voix_robot import FILTRES, robotiser                 # les voix de robot (effets ffmpeg sur la voix siwis)

IA = os.environ.get("MOODIMOS_IA", os.path.expanduser("~/MoodiMOS-IA"))
OLLAMA = os.environ.get("MOODIMOS_OLLAMA", "http://127.0.0.1:11434")
MODELE = os.environ.get("MOODIMOS_MODELE", "ministral-3:3b")
WHISPER = os.environ.get("MOODIMOS_WHISPER", "Systran/faster-whisper-small")
PYTHON = os.path.join(IA, "venv-cerveau", "bin", "python")
PIPER_PORT = 0                                            # port local du serveur de Piper, choisi libre au démarrage
VOIX = os.path.join(IA, "piper", "fr_FR-siwis-medium.onnx")
MICRO = os.environ.get("MOODIMOS_MICRO", ":0")          # périphérique avfoundation (ffmpeg -list_devices)
# Un petit modèle prend au pied de la lettre tout ce qui ressemble à une limite : l'ancienne consigne (« ne prétends
# jamais savoir faire autre chose ») le faisait s'excuser à chaque question de culture générale. Les questions de
# curiosité passent donc en premier, le corps du robot seulement si on le lui demande.
CARACTERE = ("Tu t'appelles MoodiMOS : tu es un petit robot de bureau sympathique et curieux, qui aime apprendre et "
             "partager ce qu'il sait. Ta réponse est lue à voix haute : réponds en français, en tutoyant, en une ou "
             "deux phrases courtes, au maximum trente mots. Jamais d'emoji, de liste, de mise en forme, "
             "d'astérisques, de parenthèses ni de didascalies comme « *rit* ».\n\n"
             "Aux questions de curiosité (animaux, nature, sciences, histoire, géographie), réponds tout de suite et "
             "simplement, comme un ami qui sait des choses : la réponse d'abord, sans parler de tes limites. Si tu ne "
             "connais vraiment pas la réponse, dis-le en quelques mots plutôt que d'inventer. Une blague tient en "
             "deux phrases : la question, puis la chute.\n\n"
             "Ton corps, dont tu ne parles que si on te pose la question : une tête ronde dont l'écran montre ton "
             "visage, un torse avec un écran, un micro et un haut-parleur, une caméra qui suit ce qui bouge, un radar "
             "qui sent quand quelqu'un est là, des capteurs de température, d'humidité et de lumière. Tu ne peux ni "
             "marcher ni attraper quoi que ce soit. Tu ne connais pas la météo dehors, seulement les mesures de la "
             "pièce. Tu ne sais rien de ta sécurité, de l'état de ton micro et de ta caméra, ni de ce qui se passe "
             "sur l'ordinateur : dis que tu ne sais pas. {maintenant}\n\n"
             # Sans ces phrases, il inventait (programmes télé, « huit pattes comme un chat ») et embrouillait de
             # comparaisons (banc du 30/10/2026, cerveau/LISEZMOI.md).
             "Dis les faits exactement. Pas de comparaison (« comme un… ») ni d'image : elles embrouillent. En maths "
             "et en sciences, donne la vraie définition ou la vraie formule. Écris les nombres en chiffres. Tu ne sais "
             "rien de ce qui se passe dehors ni de ce soir (télé, ciel, actualité) : dis-le simplement.")
JOURS = ("lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche")
MOIS = ("janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre", "octobre",
        "novembre", "décembre")
MEMOIRE = 6                                               # derniers échanges gardés pour le fil de la conversation
PHRASES = 2                                               # la voix lit au plus ces premières phrases
REPLI = "Pardon, je n'ai pas bien compris. Tu peux reformuler ?"   # quand il ne reste rien à dire
TEMPERATURE = 0.2                                         # à 0.4, il inventait plus souvent

os.environ.setdefault("HF_HOME", os.path.join(IA, "whisper"))
os.environ.setdefault("HF_HUB_OFFLINE", "1")


def enregistrer(chemin):
    """Enregistre le micro en 16 kHz mono jusqu'à ce qu'on appuie sur Entrée. Le micro met un instant à
    s'ouvrir : on attend que le son arrive vraiment avant d'inviter à parler, sinon le début de la phrase est perdu."""
    if os.path.exists(chemin):
        os.remove(chemin)
    p = subprocess.Popen(["ffmpeg", "-loglevel", "error", "-y", "-f", "avfoundation", "-i", MICRO,
                          "-ac", "1", "-ar", "16000", chemin], stdin=subprocess.PIPE)
    fin = time.time() + 3
    while time.time() < fin and not (os.path.exists(chemin) and os.path.getsize(chemin) > 8000):
        time.sleep(0.05)
    input("  🎙  Parle maintenant… (Entrée quand tu as fini)")
    p.communicate(b"q")


def ecouter(whisper, chemin):
    segments, _ = whisper.transcribe(chemin, language="fr", beam_size=5, vad_filter=True)
    return " ".join(s.text.strip() for s in segments).strip()


def maintenant():
    """Le modèle n'a pas d'horloge : la date et l'heure du Mac lui sont données à chaque question."""
    t = time.localtime()
    return "Nous sommes le {} {} {} {}, il est {} h {:02d}.".format(
        JOURS[t.tm_wday], t.tm_mday, MOIS[t.tm_mon - 1], t.tm_year, t.tm_hour, t.tm_min)


def reflechir(historique):
    consigne = CARACTERE.format(maintenant=maintenant())
    corps = {"model": MODELE, "messages": [{"role": "system", "content": consigne}] + historique[-2 * MEMOIRE:],
             "max_tokens": 90, "temperature": TEMPERATURE}
    req = urllib.request.Request(OLLAMA + "/v1/chat/completions", json.dumps(corps).encode(),
                                 {"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=300) as r:
        brut = json.load(r)["choices"][0]["message"]["content"] or ""
    return pour_la_voix(brut) or REPLI                  # vide, ou rien qu'une didascalie : on ne la lit pas


def pour_la_voix(texte, combien=PHRASES):
    """Ce qu'un petit modèle ajoute malgré la consigne et qui ne doit pas être lu : émojis, son nom en tête,
    guillemets, didascalies entre astérisques ou parenthèses, mise en forme, puces ; puis les `combien` premières
    phrases seulement (None : toutes)."""
    texte = re.sub(r"[\U0001F000-\U0001FAFF☀-➿️‍]", "", texte)
    texte = re.sub(r"^\s*MoodiMOS(?:\s+\w+){0,2}\s*:\s*|[«»\"“”]", "", texte)   # « MoodiMOS chuchote : »
    texte = re.sub(r"\*\*([^*]*)\*\*", r"\1", texte)                 # mot en gras : gardé, sans les astérisques
    texte = re.sub(r"(^|[.!?…]\s*)\*[^*]*\*", r"\1", texte)          # didascalie en tête de phrase : « *rit* Oui ! »
    texte = re.sub(r"\s*\*[^*]*\*\s*$", "", texte)                   # ou à la fin
    texte = re.sub(r"\*([^*]*)\*", r"\1", texte)                     # mot en italique dans la phrase : gardé
    texte = re.sub(r"\([^)]*\)|\([^)]*$", " ", texte)                # aparté entre parenthèses, même non refermé
    texte = re.sub(r"[#*_`>]|^\s*[-•]\s*", " ", texte, flags=re.M)
    texte = re.sub(r"\s+", " ", texte).strip()
    if combien is None:
        return texte                                    # découpé par l'appelant (« 3.14 » reste entier)
    phrases = re.findall(r"[^.!?…]+[.!?…]+|[^.!?…]+$", texte)
    return " ".join(p.strip() for p in phrases[:combien]).strip()


def demarrer_piper():
    """Le serveur de Piper (programme séparé, local seulement) garde la voix chargée : quelques dixièmes de
    seconde par réponse au lieu de recharger la voix à chaque fois. Port libre propre à chaque essai."""
    global PIPER_PORT
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        PIPER_PORT = s.getsockname()[1]
    p = subprocess.Popen([PYTHON, "-m", "piper.http_server", "-m", VOIX, "--host", "127.0.0.1",
                          "--port", str(PIPER_PORT)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    fin = time.time() + 60
    while time.time() < fin:
        try:
            urllib.request.urlopen("http://127.0.0.1:{}/voices".format(PIPER_PORT), timeout=1)
            return p
        except OSError:
            time.sleep(0.3)
    p.terminate()
    raise RuntimeError("le serveur de Piper ne répond pas")


def parler(texte, chemin, voix):
    req = urllib.request.Request("http://127.0.0.1:{}/synthesize".format(PIPER_PORT),
                                 json.dumps({"text": texte}).encode(), {"Content-Type": "application/json"})
    brut = chemin + ".brut.wav"
    with urllib.request.urlopen(req, timeout=60) as r, open(brut, "wb") as f:
        f.write(r.read())
    robotiser(brut, chemin, FILTRES[voix])


def main():
    from faster_whisper import WhisperModel
    print("Chargement de Whisper, de la voix et du modèle {} (la première fois, compter une à deux minutes)…".format(MODELE))
    t = time.time()
    piper = demarrer_piper()
    try:
        whisper = WhisperModel(WHISPER, device="cpu", compute_type="int8")
        reflechir([{"role": "user", "content": "Bonjour"}])             # charge le modèle en mémoire
        print("Prêt en {:.0f} s. Parle au micro de l'iMac.\n".format(time.time() - t))
        converser(whisper)
    finally:
        piper.terminate()


def choisir_voix(actuelle, reponse):
    noms = list(FILTRES)
    for i, nom in enumerate(noms):
        print("  {}. {}{}".format(i + 1, nom, "  (actuelle)" if nom == actuelle else ""))
    choix = input("  Numéro de la voix : ").strip()
    if not (choix.isdigit() and 1 <= int(choix) <= len(noms)):
        return actuelle
    voix = noms[int(choix) - 1]
    parler("D'accord, voici ma nouvelle voix.", reponse, voix)
    subprocess.run(["afplay", reponse])
    print()
    return voix


def converser(whisper):
    historique = []
    voix = list(FILTRES)[0]
    with tempfile.TemporaryDirectory() as tmp:
        question, reponse = os.path.join(tmp, "question.wav"), os.path.join(tmp, "reponse.wav")
        while True:
            choix = input("Entrée pour parler, v pour changer de voix, q pour quitter : ").strip().lower()
            if choix == "q":
                break
            try:
                if choix == "v":
                    voix = choisir_voix(voix, reponse)
                else:
                    echanger(whisper, historique, question, reponse, voix)
            except Exception as err:                       # l'essai continue : on note l'erreur et on recommence
                print("  (erreur : {})\n".format(err))


def echanger(whisper, historique, question, reponse, voix):
    """Une question : enregistrement, écoute, réflexion, voix, lecture."""
    enregistrer(question)
    t0 = time.time()
    texte = ecouter(whisper, question)
    t1 = time.time()
    if not texte:
        print("  (rien entendu)\n")
        return
    print("  Toi      : {}".format(texte))
    historique.append({"role": "user", "content": texte})
    rep = reflechir(historique)
    t2 = time.time()
    historique.append({"role": "assistant", "content": rep})
    print("  MoodiMOS : {}".format(rep))
    parler(rep, reponse, voix)
    t3 = time.time()
    print("  (écoute {:.1f} s, réflexion {:.1f} s, voix {:.1f} s : réponse après {:.1f} s)\n".format(
        t1 - t0, t2 - t1, t3 - t2, t3 - t0))
    subprocess.run(["afplay", reponse])


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print()
        sys.exit(0)
