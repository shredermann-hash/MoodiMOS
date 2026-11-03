# MoodiMOS — local brain test without the robot: Mac’s mic and speakers
# 
# Full chain: mic recording → Whisper (voice → text) → local Ollama model
# (text → response) → Piper, siwis voice (response → voice) → playback. The robot will later replicate this, with the Mac acting as the "brain" via Wi-Fi.
# 
# Prerequisites (see `brain/README.md`): Ollama server running, model downloaded, Python environment with `faster-whisper` and `piper-tts`, siwis voice downloaded. Run:
# ```
# ~/MoodiMOS-IA/venv-cerveau/bin/python brain/conversation_test.py
# ```
# Press Enter to start speaking, Enter to finish; use "v" to switch voices, "q" to quit.
# 
# Piper is under GPL-3.0: runs as a standalone program (its own small server, voice cached in memory), never imported (same rule as R3B00T).

import json, os, re, socket, subprocess, sys, tempfile, time, urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from robot_voice import FILTRES, robotiser                 # **Robot voices (FFmpeg voice effects on *siwis* audio)**

IA = os.environ.get("MOODIMOS_IA", os.path.expanduser("~/MoodiMOS-IA"))
OLLAMA = os.environ.get("MOODIMOS_OLLAMA", "http://127.0.0.1:11434")
MODELE = os.environ.get("MOODIMOS_MODELE", "ministral-3:3b")
WHISPER = os.environ.get("MOODIMOS_WHISPER", "Systran/faster-whisper-small")
PYTHON = os.path.join(IA, "venv-cerveau", "bin", "python")
PIPER_PORT = 0                                            # Local port for the Piper server, selected freely at startup.
VOIX = os.path.join(IA, "piper", "fr_FR-siwis-medium.onnx")
MICRO = os.environ.get("MOODIMOS_MICRO", ":0")          # `avfoundation -list_devices`
# A minimal model strictly adheres to any resemblance of a boundary: the old directive ("never claim to know anything else") caused it to apologize for every general knowledge query. Curiosity-driven questions take precedence; the robot’s body is only activated upon explicit request.
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
             # Without these phrases, he was inventing (TV programs, *"eight legs like a cat"*) and confusing comparisons (bench on 30/10/2026, brain/README.md).
             "Dis les faits exactement. Pas de comparaison (« comme un… ») ni d'image : elles embrouillent. En maths "
             "et en sciences, donne la vraie définition ou la vraie formule. Écris les nombres en chiffres. Tu ne sais "
             "rien de ce qui se passe dehors ni de ce soir (télé, ciel, actualité) : dis-le simplement.")
JOURS = ("lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche")
MOIS = ("janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre", "octobre",
        "novembre", "décembre")
MEMOIRE = 6                                               # last exchanges retained for the chat thread.
PHRASES = 2                                               # The voice reads these first sentences.
REPLI = "Pardon, je n'ai pas bien compris. Tu peux reformuler ?"   # when nothing remains to say
TEMPERATURE = 0.2                                         # At 0.4, he was inventing more frequently.

os.environ.setdefault("HF_HOME", os.path.join(IA, "whisper"))
os.environ.setdefault("HF_HUB_OFFLINE", "1")


def enregistrer(chemin):
    'Record the microphone at 16 kHz mono until Enter is pressed. The microphone takes a moment to open—wait for the sound to actually arrive before prompting to speak, otherwise the start of the phrase is lost.'
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
    'The model lacks a clock: the Mac’s date and time are provided on each query.'
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
    return pour_la_voix(brut) or REPLI                  # empty or only a stage direction: do not read it aloud


def pour_la_voix(texte, combien=PHRASES):
    'What a small model adds despite instructions and should not be read: emojis, name at the top,\n    quotation marks, stage directions in asterisks or parentheses, formatting, bullet points; then the first `combien` sentences only (None: all).'
    texte = re.sub(r"[\U0001F000-\U0001FAFF☀-➿️‍]", "", texte)
    texte = re.sub(r"^\s*MoodiMOS(?:\s+\w+){0,2}\s*:\s*|[«»\"“”]", "", texte)   # *« MoodiMOS whispers: »*
    texte = re.sub(r"\*\*([^*]*)\*\*", r"\1", texte)                 # bold text: kept, without asterisks
    texte = re.sub(r"(^|[.!?…]\s*)\*[^*]*\*", r"\1", texte)          # Head caption: *« *laughs* Yes! »**
    texte = re.sub(r"\s*\*[^*]*\*\s*$", "", texte)                   # at the end
    texte = re.sub(r"\*([^*]*)\*", r"\1", texte)                     # *kept* in italics within the sentence: retained
    texte = re.sub(r"\([^)]*\)|\([^)]*$", " ", texte)                # *(apostrophe in parentheses, even if not closed)*
    texte = re.sub(r"[#*_`>]|^\s*[-•]\s*", " ", texte, flags=re.M)
    texte = re.sub(r"\s+", " ", texte).strip()
    if combien is None:
        return texte                                    # cut by the caller ("3.14" remains integer)
    phrases = re.findall(r"[^.!?…]+[.!?…]+|[^.!?…]+$", texte)
    return " ".join(p.strip() for p in phrases[:combien]).strip()


def demarrer_piper():
    'Piper’s local-only server (separate program) maintains loaded voice: tenths of a second per response instead of reloading voice per attempt. Free port per test.'
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
    raise RuntimeError('The Piper server is unresponsive.')


def parler(texte, chemin, voix):
    req = urllib.request.Request("http://127.0.0.1:{}/synthesize".format(PIPER_PORT),
                                 json.dumps({"text": texte}).encode(), {"Content-Type": "application/json"})
    brut = chemin + ".brut.wav"
    with urllib.request.urlopen(req, timeout=60) as r, open(brut, "wb") as f:
        f.write(r.read())
    robotiser(brut, chemin, FILTRES[voix])


def main():
    from faster_whisper import WhisperModel
    print('Loading Whisper, the voice and model {} (allow one to two minutes on the first run)…'.format(MODELE))
    t = time.time()
    piper = demarrer_piper()
    try:
        whisper = WhisperModel(WHISPER, device="cpu", compute_type="int8")
        reflechir([{"role": "user", "content": "Bonjour"}])             # Load the model into memory
        print('Ready in {:.0f} s. Speak into the Mac microphone.\n'.format(time.time() - t))
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
            except Exception as err:                       # Continuation test: error recorded → restart.
                print('  (error: {})\n'.format(err))


def echanger(whisper, historique, question, reponse, voix):
    'Documentation for Handmade Robot:\n- Head (head): Audio recording, playback, cognitive processing, voice synthesis, text-to-speech (lecture).\n- Pitch (pas): Connector spacing for mechanical/acoustic interfaces.'
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
    print('  (listening {:.1f} s, thinking {:.1f} s, voice {:.1f} s: answer after {:.1f} s)\n'.format(
        t1 - t0, t2 - t1, t3 - t2, t3 - t0))
    subprocess.run(["afplay", reponse])


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print()
        sys.exit(0)
