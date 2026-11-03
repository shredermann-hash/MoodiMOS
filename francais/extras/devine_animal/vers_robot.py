# MoodiMOS — « Devine l'animal » dans le robot : tout se prépare sur le Mac, le robot joue seul.
#
# Le moteur du jeu est porté dans le torse (firmware/moodimos-torse/GuessGame.h) : mêmes probabilités que le
# prototype (devine_animal.py). La petite répond avec les boutons de l'écran du torse (pouce vert, pouce rouge,
# point d'interrogation) ; le robot dit les phrases enregistrées ici avec la voix siwis.
#
#   … vers_robot.py table              écrit firmware/moodimos-torse/GuessData.h depuis animaux.json
#   … vers_robot.py voix [--voix 1]    fabrique les phrases (Piper, voix siwis) dans ~/MoodiMOS-IA/devine_robot,
#                                      avec celles des humeurs de l'édition complète (HUMEURS)
#   … vers_robot.py envoyer [port]     les range dans la mémoire flash du torse, par l'USB (commande « deposer »)
#
# Lancer avec ~/MoodiMOS-IA/venv-cerveau/bin/python (Piper y est installé). Piper est sous GPL-3.0 : il tourne
# comme programme séparé, jamais importé. Ferme d'abord tout moniteur série du torse.

import argparse, glob, json, os, re, select, subprocess, sys, termios, time, wave

ICI = os.path.dirname(os.path.abspath(__file__))
RACINE = os.path.normpath(os.path.join(ICI, "..", ".."))
TABLEAU = os.path.join(ICI, "animaux.json")
ENTETE = os.path.join(RACINE, "firmware", "moodimos-torse", "GuessData.h")
IA = os.environ.get("MOODIMOS_IA", os.path.expanduser("~/MoodiMOS-IA"))
PIPER = os.path.join(IA, "venv-cerveau", "bin", "piper")
VOIX = os.path.join(IA, "piper", "fr_FR-siwis-medium.onnx")
SORTIE = os.path.join(IA, "devine_robot")
FLASH_KO = 8600          # partition FAT de 9 Mo du torse, moins sa table et son nivellement d'usure

# Phrases fixes : nom du fichier sur le robot → texte dit. Les questions et les animaux suivent (devine_q00…,
# devine_a00…), dans l'ordre d'animaux.json.
FIXES = {
    "devine_intro": "Coucou ! Pense très fort à un animal, mais ne me dis pas lequel ! Je vais le deviner. "
                    "Pour me répondre, appuie sur les boutons : le pouce vert pour oui, le pouce rouge pour non, "
                    "et le point d'interrogation si tu ne sais pas.",
    "devine_go": "Tu as choisi ton animal ? On y va !",
    "devine_encore": "Oh ! Alors je réfléchis encore…",
    "devine_youpi": "Youpi ! J'ai deviné !",
    "devine_bravo": "Tu m'as battu ! Bravo, tu es trop forte !",
    "devine_rejoue": "On rejoue ?",
    "devine_bientot": "D'accord ! À bientôt !",
}
# Phrases des humeurs, hors du jeu : dites par le torse quand elles sont rangées dans sa flash (sinon un cri).
HUMEURS = {
    "secoue": "Ne me secoue pas comme un prunier ! Je ne suis pas ton jouet !",   # la colère monte (3e bousculade)
}
# Lettres de la police au trait de l'écran du torse (DashboardNeon.cpp), en capitales
ECRAN = set(" '!,-./:?%+0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZÀÂÇÈÉÊÎÔÙÛ")


def charger():
    t = json.load(open(TABLEAU, encoding="utf-8"))
    questions = [(k, v if isinstance(v, dict) else {"texte": v}) for k, v in t["questions"].items()]
    return questions, t["animaux"]


def phrases():
    questions, animaux = charger()
    p = dict(FIXES)
    p.update(HUMEURS)
    for i, (_, q) in enumerate(questions):
        p["devine_q%02d" % i] = q["texte"]
    for i, a in enumerate(animaux):
        p["devine_a%02d" % i] = "Je crois que c'est… {} ! J'ai trouvé ?".format(a["nom"])
    return p


def pour_ecran(texte):
    t = texte.upper().replace("Œ", "OE").replace("…", "...")
    manque = set(t) - ECRAN
    if manque:
        sys.exit("lettres absentes de la police de l'écran : " + "".join(sorted(manque)) + " dans « " + texte + " »")
    return t


def c_texte(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def table():
    questions, animaux = charger()
    cles = [k for k, _ in questions]
    if len(cles) > 64:
        sys.exit("64 questions au plus (masques de 64 bits)")
    lignes = [
        "// Table du jeu « Devine l'animal » (édition complète), écrite par extras/devine_animal/vers_robot.py depuis",
        "// animaux.json : ne pas modifier à la main. Pour chaque animal, les questions où la réponse est oui, et",
        "// celles où elle dépend (bit n = question n) ; toutes les autres valent non. Une question « inverse »",
        "// attend non pour un oui du tableau. Textes en capitales, pour la police au trait de l'écran du torse.",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "namespace GuessData {",
        "",
        "struct Question { const char* text; bool inverse; };",
        "struct Animal { const char* name; uint64_t yes, maybe; };",
        "",
        "constexpr int QUESTIONS = %d, ANIMALS = %d;" % (len(questions), len(animaux)),
        "",
        "constexpr Question QUESTION[QUESTIONS] = {",
    ]
    for k, q in questions:
        lignes.append("  {%s, %s},   // %s" % (c_texte(pour_ecran(q["texte"])), "true" if q.get("inverse") else "false", k))
    lignes += ["};", "", "constexpr Animal ANIMAL[ANIMALS] = {"]
    for a in animaux:
        for k in a["oui"] + a["doute"]:
            if k not in cles:
                sys.exit("question inconnue « {} » pour {}".format(k, a["nom"]))
        oui = sum(1 << cles.index(k) for k in a["oui"])
        doute = sum(1 << cles.index(k) for k in a["doute"])
        lignes.append("  {%s, 0x%016xULL, 0x%016xULL}," % (c_texte(pour_ecran(a["nom"])), oui, doute))
    lignes += ["};", "", "}  // namespace GuessData", ""]
    open(ENTETE, "w", encoding="utf-8").write("\n".join(lignes))
    print("{} : {} questions, {} animaux".format(os.path.relpath(ENTETE, RACINE), len(questions), len(animaux)))


def voix(numero):
    sys.path.insert(0, os.path.join(RACINE, "cerveau"))
    from voix_robot import FILTRES, robotiser
    filtre = list(FILTRES.values())[numero - 1]
    os.makedirs(SORTIE, exist_ok=True)
    total = 0
    for nom, texte in phrases().items():
        brut, final = os.path.join(SORTIE, nom + ".brut.wav"), os.path.join(SORTIE, nom + ".wav")
        subprocess.run([PIPER, "-m", VOIX, "-f", brut], input=texte.encode(), check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        robotiser(brut, brut + ".robot.wav", filtre)
        # PCM 16 bits mono 16 kHz, ce que lit le torse ; crête à 70 % comme les autres phrases
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", brut + ".robot.wav", "-af",
                        "loudnorm=I=-18:TP=-3,aresample=16000", "-ac", "1", "-ar", "16000", "-sample_fmt", "s16",
                        final], check=True)
        os.remove(brut)
        os.remove(brut + ".robot.wav")
        total += os.path.getsize(final)
        print("  {:<15} {}".format(nom, texte))
    print("{} phrases, {:.0f} Ko pour {} Ko de flash dans le torse".format(len(phrases()), total / 1024, FLASH_KO))
    if total / 1024 > FLASH_KO:
        sys.exit("trop gros pour la flash du torse")


def ouvrir(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attr = termios.tcgetattr(fd)
    attr[0] = attr[1] = attr[3] = 0
    attr[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attr[4] = attr[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attr)
    time.sleep(0.2)
    termios.tcflush(fd, termios.TCIFLUSH)
    return fd


def attendre(fd, motif, delai):
    """Lit jusqu'à voir `motif` (expression rationnelle sur les octets) ; rend tout ce qui a été lu."""
    rep, fin = b"", time.time() + delai
    while not re.search(motif, rep) and time.time() < fin:
        if select.select([fd], [], [], 0.05)[0]:
            try:
                rep += os.read(fd, 4096)
            except BlockingIOError:
                pass
    return rep


def ecrire(fd, octets):
    vue = memoryview(octets)
    while vue:
        try:
            vue = vue[os.write(fd, vue):]
        except BlockingIOError:
            select.select([], [fd], [], 0.5)


def deposer(fd, nom, donnees):
    """« deposer <nom> <octets> », puis le fichier par morceaux de 4 Ko, chacun confirmé par un « + »."""
    ecrire(fd, b"deposer %s %d\n" % (nom.encode(), len(donnees)))
    rep = attendre(fd, rb"pret|refus", 30.0)          # la première fois, le torse formate sa partition
    if b"pret" not in rep:
        return rep.decode("utf-8", "replace").strip() or "pas de réponse"
    for k in range(0, len(donnees), 4096):
        ecrire(fd, donnees[k:k + 4096])
        if b"+" not in attendre(fd, rb"\+", 5.0):
            return "pas de confirmation au morceau %d" % (k // 4096)
    rep = attendre(fd, rb"deposer[^\n]*\n", 5.0).decode("utf-8", "replace")
    somme = sum(donnees) & 0xFFFFFFFF
    return None if "somme %d" % somme in rep else rep.strip()


def envoyer(port):
    if not port:
        ports = sorted(glob.glob("/dev/cu.usbmodem*"))
        if not ports:
            sys.exit("torse introuvable sur l'USB")
        port = ports[0]
    noms = list(phrases())
    absents = [n for n in noms if not os.path.exists(os.path.join(SORTIE, n + ".wav"))]
    if absents:
        sys.exit("phrases à fabriquer d'abord (… vers_robot.py voix) : " + ", ".join(absents[:5]))
    fd = ouvrir(port)
    t0 = time.time()
    try:
        for i, nom in enumerate(noms):
            donnees = open(os.path.join(SORTIE, nom + ".wav"), "rb").read()
            for essai in range(3):   # un raté passager arrive (journal de la caméra mêlé aux réponses)
                erreur = deposer(fd, nom, donnees)
                if not erreur:
                    break
                print("\n  {} : {}, nouvel essai".format(nom, erreur))
                time.sleep(3)        # le torse abandonne la réception (1 s sans octet) et lit la suite comme des
                termios.tcflush(fd, termios.TCIOFLUSH)   # commandes inconnues : on attend qu'il ait fini
            if erreur:
                sys.exit("{} : {}".format(nom, erreur))
            print("\r  {}/{} {}".format(i + 1, len(noms), nom).ljust(40), end="", flush=True)
    finally:
        os.close(fd)
    print("\n{} phrases rangées dans le torse en {:.0f} s.".format(len(noms), time.time() - t0))


def main():
    ap = argparse.ArgumentParser(description="Devine l'animal : table et phrases pour le robot")
    ap.add_argument("action", choices=["table", "voix", "envoyer"])
    ap.add_argument("port", nargs="?")
    ap.add_argument("--voix", type=int, default=1, help="voix de cerveau/voix_robot.py (1 = siwis, par défaut)")
    args = ap.parse_args()
    if args.action == "table":
        table()
    elif args.action == "voix":
        voix(args.voix)
    else:
        envoyer(args.port)


if __name__ == "__main__":
    main()
