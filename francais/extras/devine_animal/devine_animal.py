# MoodiMOS — « Devine l'animal » : prototype sur le Mac, avant de passer dans le robot
#
# L'enfant pense à un animal ; le robot pose des questions et l'enfant répond à voix haute : oui, non, ça dépend ou
# je ne sais pas (comme les jouets du commerce, Magic Jinn ou Lexibook). Aucune IA :
# un tableau d'animaux (animaux.json) et des probabilités. Chaque réponse renforce les animaux qui collent et affaiblit
# les autres, sans les éliminer (une réponse de travers d'un enfant de 5 ans par exemple ne fait pas perdre la partie) ; la
# question suivante est celle qui départage le mieux les animaux encore probables. Le robot propose un animal quand
# il en est assez sûr ; s'il se trompe, il continue et en propose un autre (trois essais).
#
# Voix : Piper (voix siwis ou une voix de robot de cerveau/voix_robot.py). Écoute : micro du Mac, fin de la réponse
# détectée au silence, Whisper pour reconnaître oui / non / je ne sais pas.
#
# Lancer (serveur Ollama inutile ici) :
#   ~/MoodiMOS-IA/venv-cerveau/bin/python extras/devine_animal/devine_animal.py
#   … --voix 4          voix n° 4 de la liste de voix_robot.py (1 = siwis, par défaut)
#   … --clavier         réponses au clavier (o / n / ?) au lieu du micro
#   … --simulation      fait jouer tous les animaux sans voix ni micro, pour vérifier le tableau

import argparse, json, math, os, random, re, subprocess, sys, tempfile, time, unicodedata, wave

ICI = os.path.dirname(os.path.abspath(__file__))
TABLEAU = os.path.join(ICI, "animaux.json")
FIABLE = 0.9          # probabilité qu'un enfant réponde comme le tableau (le reste : erreurs, hésitations)
SUR, AVANCE = 0.8, 8.0   # il propose un animal assez probable, ou nettement devant le deuxième (trois essais)
QUESTIONS_MIN, QUESTIONS_MAX, ESSAIS = 3, 14, 3
VOCABULAIRE = [" Oui", " oui", "Oui", " Non", " non", "Non", " Ouais", " ouais", " Nan", " nan", " Je", " je",
               " ne", " sais", " pas", " Si", " si", " Euh", " euh", " Ça", " ça", " dépend", " Un", " un",
               " peu", " Des", " des", " fois", " Parfois", " parfois", " toujours", ".", "!", ",", "?", "…"]


# ── le moteur (sans voix ni micro) ──

class Partie:
    def __init__(self, tableau):
        self.questions = {k: (v if isinstance(v, dict) else {"texte": v}) for k, v in tableau["questions"].items()}
        self.animaux = tableau["animaux"]
        self.proba = [1.0 / len(self.animaux)] * len(self.animaux)
        self.posees, self.rates = [], set()

    def oui_probable(self, a, q):
        """Probabilité que l'enfant réponde oui à la question q en pensant à l'animal a."""
        val = 1.0 if q in a["oui"] else 0.5 if q in a["doute"] else 0.0
        if self.questions[q].get("inverse"):
            val = 1.0 - val
        return FIABLE * val + (1 - FIABLE) * (1 - val)

    def repondre(self, q, reponse):
        self.posees.append(q)
        if reponse not in ("oui", "non", "depend"):
            return
        for i, a in enumerate(self.animaux):
            if reponse == "depend":                    # « ça dépend » : vrai surtout des animaux notés « doute »
                self.proba[i] *= 0.8 if q in a["doute"] else 0.2
                continue
            p = self.oui_probable(a, q)
            self.proba[i] *= p if reponse == "oui" else 1 - p
        self._normaliser()

    def ecarter(self, i):
        """Le robot a proposé l'animal i et l'enfant a dit non."""
        self.rates.add(i)
        self.proba[i] = 0.0
        self._normaliser()

    def _normaliser(self):
        s = sum(self.proba)
        self.proba = [p / s for p in self.proba] if s > 0 else [0.0 if i in self.rates else 1.0 for i in range(len(self.proba))]
        s = sum(self.proba)
        self.proba = [p / s for p in self.proba]

    def meilleure_question(self):
        """Question au plus grand gain d'information (celle qui départage le mieux les animaux probables)."""
        def h(ps):
            return -sum(p * math.log2(p) for p in ps if p > 0)
        base, choix, gain_max = h(self.proba), None, 0.02
        for q in self.questions:
            if q in self.posees:
                continue
            oui = [p * self.oui_probable(a, q) for p, a in zip(self.proba, self.animaux)]
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
        """("question", clé) ou ("proposer", indice d'animal)."""
        i, p = self.favori()
        q = self.meilleure_question()
        n = len(self.posees)
        second = sorted(self.proba)[-2]
        devant = p >= SUR or p >= AVANCE * second                 # une réponse de travers ne le fait plus tourner en rond
        if q is None or n >= QUESTIONS_MAX or (devant and n >= QUESTIONS_MIN):
            return "proposer", i
        return "question", q


def simuler(tableau, erreur, tours=20):
    """Chaque animal joué `tours` fois par un enfant virtuel ; « doute » = réponse au hasard ; erreur = taux de
    réponses de travers. Rend le taux de réussite et le nombre moyen de questions."""
    gagnes, total, nq = 0, 0, []
    rates = {}
    for _ in range(tours):
        for cible, a in enumerate(tableau["animaux"]):
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


# ── la voix et l'écoute ──

def comprendre(texte):
    """oui / non / depend (ça dépend, un peu, des fois) / jsp (je ne sais pas) / None, d'après ce que Whisper a entendu."""
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
    """Micro du Mac ouvert pendant toute la partie (aucun délai d'ouverture après une question). Un fil de lecture
    range chaque dixième de seconde avec son heure et son niveau ; écouter() prend le son à partir de la fin de la
    phrase du robot, démarre au premier son plus fort que le bruit de la pièce et s'arrête après un court silence."""
    TAUX, BLOC = 16000, 1600          # 0,1 s par bloc

    def __init__(self, micro=":0", debug=False):
        import array, collections, threading
        os.environ.setdefault("HF_HOME", os.path.join(os.environ.get("MOODIMOS_IA", os.path.expanduser("~/MoodiMOS-IA")), "whisper"))
        os.environ.setdefault("HF_HUB_OFFLINE", "1")
        self.array, self.debug = array, debug
        self.blocs = collections.deque(maxlen=600)          # 60 s de son : (heure, données, niveau)
        self.verrou = threading.Lock()
        self.micro = subprocess.Popen(["ffmpeg", "-loglevel", "error", "-f", "avfoundation", "-i", micro, "-ac", "1",
                                       "-ar", str(self.TAUX), "-f", "s16le", "-"], stdout=subprocess.PIPE,
                                      stderr=subprocess.DEVNULL)
        threading.Thread(target=self._lire, daemon=True).start()
        from faster_whisper import WhisperModel                 # le bruit de la pièce se mesure pendant ce chargement
        self.whisper = WhisperModel("Systran/faster-whisper-small", device="cpu", compute_type="int8")
        # Whisper ne peut écrire que le vocabulaire du jeu : un « oui » d'enfant, court et aigu, n'est plus pris pour une
        # phrase inventée (« Comme Catherine »). Les jetons spéciaux (début, langue, fin) restent permis.
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
        """Bruit de fond : niveau du quart le plus calme des 20 dernières secondes."""
        with self.verrou:
            niveaux = sorted(b[2] for b in list(self.blocs)[-200:])
        return niveaux[len(niveaux) // 4] if niveaux else 100.0

    def enregistrer(self, chemin, depuis, attente=7.0, silence=0.6, maxi=5.0):
        seuil = max(2.0 * self.bruit(), 180.0)             # une voix d'enfant, de loin, est douce
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
        morceau = [d for t, d, _ in blocs[max(debut - 3, 0):] if t <= fin]   # 0,3 s avant le premier son
        with wave.open(chemin, "wb") as w:
            w.setnchannels(1), w.setsampwidth(2), w.setframerate(self.TAUX)
            w.writeframes(b"".join(morceau))
        return True

    def ecouter(self, chemin, depuis):
        if not self.enregistrer(chemin, depuis):
            return None, ""
        segs, _ = self.whisper.transcribe(chemin, language="fr", beam_size=3, initial_prompt="Oui. Non. Ça dépend.",
                                          suppress_tokens=self.interdits, without_timestamps=True)
        segs = [s for s in segs if s.no_speech_prob < 0.6]      # bruit pris pour de la parole : on l'ignore
        texte = " ".join(s.text.strip() for s in segs).strip()
        if segs and max(s.avg_logprob for s in segs) < -0.8:    # pas sûr : on redemande plutôt que de se tromper
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
        sys.path.insert(0, os.path.join(ICI, "..", "..", "cerveau"))
        import essai_conversation as cerveau                # serveur de Piper et voix de robot
        self.cerveau, self.oreille = cerveau, oreille
        self.voix = list(cerveau.FILTRES)[voix - 1]
        self.serveur = cerveau.demarrer_piper()
        self.tmp = tempfile.mkdtemp()

    def dire(self, texte):
        print("  MoodiMOS : " + texte)
        son = os.path.join(self.tmp, "dire.wav")
        self.cerveau.parler(texte, son, self.voix)
        subprocess.run(["afplay", son])
        self.fin_parole = time.time() + 0.15             # la fin de la phrase résonne encore un instant

    def demander(self, texte, relances=2):
        """Pose la question jusqu'à obtenir oui, non ou je ne sais pas."""
        self.dire(texte)
        for n in range(relances + 1):
            rep, entendu = self.oreille.ecouter(os.path.join(self.tmp, "reponse.wav"), self.fin_parole)
            print("  Enfant   : {}{}".format(entendu or "(rien)", "  → " + rep if rep else ""))
            if rep or n == relances:
                return rep or "jsp"
            # on répète la question elle-même : « Tu es toujours là ? » appelait un oui qui était pris pour la réponse
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
            nom = p.animaux[x]["nom"]
            rep = robot.demander("Je crois que c'est… {} ! J'ai trouvé ?".format(nom))
            if rep == "jsp":                                # pas entendu : ce n'est pas un non
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
    ap.add_argument("--debug", action="store_true", help="affiche les niveaux du micro quand rien n'est entendu")
    args = ap.parse_args()
    tableau = json.load(open(TABLEAU, encoding="utf-8"))
    if args.simulation:
        for erreur in (0.0, 0.1, 0.2):
            ok, nq, rates = simuler(tableau, erreur)
            print("réponses de travers {:>3.0%} : trouvé {:5.1%} des parties, {:.1f} questions en moyenne{}".format(
                erreur, ok, nq, "" if not rates else ", ratés : " + ", ".join(
                    "{} ×{}".format(k, v) for k, v in sorted(rates.items(), key=lambda kv: -kv[1])[:6])))
        return
    print("Chargement de la voix{}…".format("" if args.clavier else " et de l'écoute"))
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
