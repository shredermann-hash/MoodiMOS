# MoodiMOS (formerly petite) — Guess the Animal: the Mac prototype preceding the robot version.
# 
# A child thinks of an animal and answers aloud in French: oui, non, ça dépend or je ne sais pas,
# similar to commercial Magic Jinn or Lexibook toys. No language model is used: animals.json stores
# an animal table and probabilities. Each answer strengthens matching animals and weakens others,
# without eliminating them; an incorrect answer, for example from a 5-year-old, does not lose the game.
# The next question best separates the remaining likely animals. When sufficiently confident,
# the robot guesses an animal; after a rejection it continues, with three guesses at most.
# 
# Voice: Piper siwis or a robot effect from brain/robot_voice.py. Listening: the Mac microphone,
# end-of-answer silence detection, and Whisper recognition of oui / non / je ne sais pas.
# 
# Run without an Ollama server:
#   ~/MoodiMOS-IA/venv-cerveau/bin/python extras/guess_animal/guess_animal.py
#   … --voix 4          voice number 4 from robot_voice.py (1 = siwis, the default)
#   … --clavier         keyboard replies (o / n / ?) instead of the microphone
#   … --simulation      test every animal without speech or microphone input

import argparse, json, math, os, random, re, subprocess, sys, tempfile, time, unicodedata, wave

ICI = os.path.dirname(os.path.abspath(__file__))
TABLEAU = os.path.join(ICI, "animals.json")
FIABLE = 0.9          # probability of a child responding as per the table (rest: errors, hesitations)
SUR, AVANCE = 0.8, 8.0   # He proposes a highly probable animal (three trials)
QUESTIONS_MIN, QUESTIONS_MAX, ESSAIS = 3, 14, 3
VOCABULAIRE = [" Oui", " oui", "Oui", " Non", " non", "Non", " Ouais", " ouais", " Nan", " nan", " Je", " je",
               " ne", " sais", " pas", " Si", " si", " Euh", " euh", " Ça", " ça", " dépend", " Un", " un",
               " peu", " Des", " des", " fois", " Parfois", " parfois", " toujours", ".", "!", ",", "?", "…"]


# - the motor (no voice or mic)

class Partie:
    def __init__(self, tableau):
        self.questions = {k: (v if isinstance(v, dict) else {"texte": v}) for k, v in tableau["questions"].items()}
        self.animals = tableau["animals"]
        self.proba = [1.0 / len(self.animals)] * len(self.animals)
        self.posees, self.rates = [], set()

    def oui_probable(self, a, q):
        'Probability child answers "yes" to question q while thinking of animal a.'
        val = 1.0 if q in a["oui"] else 0.5 if q in a["doute"] else 0.0
        if self.questions[q].get("inverse"):
            val = 1.0 - val
        return FIABLE * val + (1 - FIABLE) * (1 - val)

    def repondre(self, q, reponse):
        self.posees.append(q)
        if reponse not in ("oui", "non", "depend"):
            return
        for i, a in enumerate(self.animals):
            if reponse == "depend":                    # “ça dépend”: mainly true for animals marked doute (uncertain)
                self.proba[i] *= 0.8 if q in a["doute"] else 0.2
                continue
            p = self.oui_probable(a, q)
            self.proba[i] *= p if reponse == "oui" else 1 - p
        self._normaliser()

    def ecarter(self, i):
        'The robot suggested animal i, and the child said no.'
        self.rates.add(i)
        self.proba[i] = 0.0
        self._normaliser()

    def _normaliser(self):
        s = sum(self.proba)
        self.proba = [p / s for p in self.proba] if s > 0 else [0.0 if i in self.rates else 1.0 for i in range(len(self.proba))]
        s = sum(self.proba)
        self.proba = [p / s for p in self.proba]

    def meilleure_question(self):
        'Information gain (most discriminative for distinguishing probable animals).'
        def h(ps):
            return -sum(p * math.log2(p) for p in ps if p > 0)
        base, choix, gain_max = h(self.proba), None, 0.02
        for q in self.questions:
            if q in self.posees:
                continue
            oui = [p * self.oui_probable(a, q) for p, a in zip(self.proba, self.animals)]
            p_oui = sum(oui)
            non = [p - o for p, o in zip(self.proba, oui)]
            if not 0 < p_oui < 1:
                continue
            gain = base - p_oui * h([o / p_oui for o in oui]) - (1 - p_oui) * h([n / (1 - p_oui) for n in non])
            if gain > gain_max:
                choix, gain_max = q, gain
        return choix

    def favori(self):
        i = max(range(len(self.proba)), key=lambda k: self.proba[k])
        return i, self.proba[i]

    def prochain_coup(self):
        '`("question", key)` or `("propose", animal clue)`.'
        i, p = self.favori()
        q = self.meilleure_question()
        n = len(self.posees)
        second = sorted(self.proba)[-2]
        devant = p >= SUR or p >= AVANCE * second                 # an inconsistent answer should not leave the game looping
        if q is None or n >= QUESTIONS_MAX or (devant and n >= QUESTIONS_MIN):
            return "proposer", i
        return "question", q


def simuler(tableau, erreur, tours=20):
    'Each virtual child plays `tours` rounds; « doubt » = random response; error = wrong-answer rate.\nReturn success rate and average questions per round.'
    gagnes, total, nq = 0, 0, []
    rates = {}
    for _ in range(tours):
        for cible, a in enumerate(tableau["animals"]):
            p = Partie(tableau)
            essais = 0
            while essais < ESSAIS:
                coup, x = p.prochain_coup()
                if coup == "proposer":
                    essais += 1
                    if x == cible:
                        gagnes += 1
                        break
                    p.ecarter(x)
                    continue
                if x in a["doute"] and random.random() < 0.4 and random.random() >= erreur:
                    p.repondre(x, "depend")
                    continue
                vrai = x in a["oui"] or (x in a["doute"] and random.random() < 0.5)
                if p.questions[x].get("inverse"):
                    vrai = not vrai
                if random.random() < erreur:
                    vrai = not vrai
                p.repondre(x, "oui" if vrai else "non")
            else:
                rates[a["nom"]] = rates.get(a["nom"], 0) + 1
            total += 1
            nq.append(len(p.posees))
    return gagnes / total, sum(nq) / len(nq), rates


# ── voice and listening ──

def comprendre(texte):
    'Yes / No / Depends (depends, a bit, sometimes) / JSP (I don’t know) / None, based on Whisper’s report.'
    t = unicodedata.normalize("NFKD", texte.lower()).encode("ascii", "ignore").decode()
    t = " " + re.sub(r"[^a-z]+", " ", t) + " "
    if re.search(r" depend | un peu | des fois | parfois | pas toujours ", t):
        return "depend"
    if re.search(r" (sais|sait) pas | peut etre ", t):
        return "jsp"
    oui = re.search(r" (oui|ouais|ouai|ouaip|si|yes|exact|exactement|voila|bien sur|d accord) ", t)
    non = re.search(r" (non|nan|no|nope|jamais|pas) ", t)
    if oui and not non:
        return "oui"
    if non and not oui:
        return "non"
    return None


class Oreille:
    'Micro left open throughout the entire session (no delay after a question). A read thread logs every tenth of a second with its timestamp and volume; `écouter()` plays audio from the robot’s phrase end, starts at the first louder-than-room-noise sample, and stops after a brief silence.'
    TAUX, BLOC = 16000, 1600          # 0.1 seconds per block

    def __init__(self, micro=":0", debug=False):
        import array, collections, threading
        os.environ.setdefault("HF_HOME", os.path.join(os.environ.get("MOODIMOS_IA", os.path.expanduser("~/MoodiMOS-IA")), "whisper"))
        os.environ.setdefault("HF_HUB_OFFLINE", "1")
        self.array, self.debug = array, debug
        self.blocs = collections.deque(maxlen=600)          # 60s of audio: (time, data, level)
        self.verrou = threading.Lock()
        self.micro = subprocess.Popen(["ffmpeg", "-loglevel", "error", "-f", "avfoundation", "-i", micro, "-ac", "1",
                                       "-ar", str(self.TAUX), "-f", "s16le", "-"], stdout=subprocess.PIPE,
                                      stderr=subprocess.DEVNULL)
        threading.Thread(target=self._lire, daemon=True).start()
        from faster_whisper import WhisperModel                 # The noise of the room is measured during this loading phase.
        self.whisper = WhisperModel("Systran/faster-whisper-small", device="cpu", compute_type="int8")
        # Whisper can only output the game’s vocabulary: a child’s short, sharp *"yes"* is no longer mistaken for an invented phrase (e.g., *"Comme Catherine"*). Special tokens (start, language, end) remain allowed.
        tok = self.whisper.hf_tokenizer
        permis = set()
        for mot in VOCABULAIRE:
            permis.update(tok.encode(mot, add_special_tokens=False).ids)
        taille = tok.get_vocab_size()
        self.interdits = [i for i in range(taille) if i not in permis and i < 50257]

    def _lire(self):
        while True:
            data = self.micro.stdout.read(self.BLOC * 2)
            if not data:
                return
            ech = self.array.array("h", data)
            rms = math.sqrt(sum(x * x for x in ech) / max(len(ech), 1))
            with self.verrou:
                self.blocs.append((time.time(), data, rms))

    def _depuis(self, t0):
        with self.verrou:
            return [b for b in self.blocs if b[0] >= t0]

    def bruit(self):
        'Background noise: quietest quarter of the last 20 seconds.'
        with self.verrou:
            niveaux = sorted(b[2] for b in list(self.blocs)[-200:])
        return niveaux[len(niveaux) // 4] if niveaux else 100.0

    def enregistrer(self, chemin, depuis, attente=7.0, silence=0.6, maxi=5.0):
        seuil = max(2.0 * self.bruit(), 180.0)             # A distant child’s voice is soft.
        debut = fin = None
        while True:
            blocs = self._depuis(depuis)
            if debut is None:
                for k, (t, _, rms) in enumerate(blocs):
                    if rms > seuil:
                        debut = k
                        break
                if debut is None and time.time() - depuis > attente:
                    if self.debug:
                        print("    (bruit {:.0f}, seuil {:.0f}, max entendu {:.0f})".format(
                            self.bruit(), seuil, max([b[2] for b in blocs] or [0])))
                    return False
            if debut is not None:
                calme = 0.0
                for t, _, rms in blocs[debut:]:
                    calme = calme + 0.1 if rms < seuil else 0.0
                    if calme >= silence or t - blocs[debut][0] > maxi:
                        fin = t
                        break
                if fin is not None:
                    break
            time.sleep(0.05)
        morceau = [d for t, d, _ in blocs[max(debut - 3, 0):] if t <= fin]   # 0.3 seconds before the first sound
        with wave.open(chemin, "wb") as w:
            w.setnchannels(1), w.setsampwidth(2), w.setframerate(self.TAUX)
            w.writeframes(b"".join(morceau))
        return True

    def ecouter(self, chemin, depuis):
        if not self.enregistrer(chemin, depuis):
            return None, ""
        segs, _ = self.whisper.transcribe(chemin, language="fr", beam_size=3, initial_prompt="Oui. Non. Ça dépend.",
                                          suppress_tokens=self.interdits, without_timestamps=True)
        segs = [s for s in segs if s.no_speech_prob < 0.6]      # noise mistaken for speech: ignore it
        texte = " ".join(s.text.strip() for s in segs).strip()
        if segs and max(s.avg_logprob for s in segs) < -0.8:    # unsure: rather request to avoid mistakes
            return None, texte + " (pas sûr)"
        return comprendre(texte), texte

    def fermer(self):
        self.micro.terminate()


class Clavier:
    def ecouter(self, chemin, depuis=None):
        r = input("  (o = oui, n = non, d = ça dépend, ? = je ne sais pas) : ").strip().lower()
        return {"o": "oui", "n": "non", "d": "depend", "?": "jsp"}.get(r[:1]), r


class Robot:
    def __init__(self, voix, oreille):
        sys.path.insert(0, os.path.join(ICI, "..", "..", "brain"))
        import conversation_test as cerveau                # Piper server and robot voice module
        self.cerveau, self.oreille = cerveau, oreille
        self.voix = list(cerveau.FILTRES)[voix - 1]
        self.serveur = cerveau.demarrer_piper()
        self.tmp = tempfile.mkdtemp()

    def dire(self, texte):
        print("  MoodiMOS : " + texte)
        son = os.path.join(self.tmp, "dire.wav")
        self.cerveau.parler(texte, son, self.voix)
        subprocess.run(["afplay", son])
        self.fin_parole = time.time() + 0.15             # end of the sentence still echoes for a moment.

    def demander(self, texte, relances=2):
        'Ask until "oui", "non", or "je ne sais pas" is received.'
        self.dire(texte)
        for n in range(relances + 1):
            rep, entendu = self.oreille.ecouter(os.path.join(self.tmp, "reponse.wav"), self.fin_parole)
            print("  Enfant   : {}{}".format(entendu or "(rien)", "  → " + rep if rep else ""))
            if rep or n == relances:
                return rep or "jsp"
            # Repeats the question itself: *"Are you still here?"*—a *"yes"* was taken as the reply.
            self.dire(("Je n'ai pas entendu. " if n == 0 else "Réponds-moi par oui, ou par non. ") + texte)

    def fermer(self):
        self.serveur.terminate()
        if hasattr(self.oreille, "fermer"):
            self.oreille.fermer()


def jouer(robot, tableau):
    robot.dire("Coucou ! Pense très fort à un animal, mais ne me dis pas lequel ! "
               "Je vais le deviner. Réponds-moi par oui, ou par non. Tu peux aussi dire : je sais pas, ou ça dépend.")
    while True:
        robot.dire("Tu as choisi ton animal ? On y va !")
        p = Partie(tableau)
        essais, trouve = 0, False
        while essais < ESSAIS:
            coup, x = p.prochain_coup()
            if coup == "question":
                p.repondre(x, robot.demander(p.questions[x]["texte"]))
                continue
            essais += 1
            nom = p.animals[x]["nom"]
            rep = robot.demander("Je crois que c'est… {} ! J'ai trouvé ?".format(nom))
            if rep == "jsp":                                # *understood*: this is not a refusal
                rep = robot.demander("Dis oui si c'est {}, ou non si ce n'est pas lui.".format(nom))
            if rep == "oui":
                trouve = True
                break
            p.ecarter(x)
            if essais < ESSAIS:
                robot.dire("Oh ! Alors je réfléchis encore…")
        if trouve:
            robot.dire("Youpi ! J'ai deviné !")
        else:
            robot.dire("Tu m'as battu ! Bravo, tu es trop forte !")
        if robot.demander("On rejoue ?", relances=1) != "oui":
            robot.dire("D'accord ! À bientôt !")
            return


def main():
    ap = argparse.ArgumentParser(description="Devine l'animal, prototype sur le Mac")
    ap.add_argument("--voix", type=int, default=1)
    ap.add_argument("--clavier", action="store_true")
    ap.add_argument("--simulation", action="store_true")
    ap.add_argument("--debug", action="store_true", help='displays the microphone levels when no sound is detected')
    args = ap.parse_args()
    tableau = json.load(open(TABLEAU, encoding="utf-8"))
    if args.simulation:
        for erreur in (0.0, 0.1, 0.2):
            ok, nq, rates = simuler(tableau, erreur)
            print('wrong answers {:>3.0%}: guessed {:5.1%} of games, {:.1f} questions on average{}'.format(
                erreur, ok, nq, "" if not rates else ", ratés : " + ", ".join(
                    "{} ×{}".format(k, v) for k, v in sorted(rates.items(), key=lambda kv: -kv[1])[:6])))
        return
    print('Loading the voice{}…'.format("" if args.clavier else " et de l'écoute"))
    robot = Robot(args.voix, Clavier() if args.clavier else Oreille(debug=args.debug))
    try:
        jouer(robot, tableau)
    finally:
        robot.fermer()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print()
