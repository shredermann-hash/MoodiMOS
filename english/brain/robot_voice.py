# **MoodiMOS — robot voice comparator: Piper’s *siwis* voice processed via FFmpeg effects**
# 
# Same phrase, multiple settings: listen, select. The chosen setting is used in the brain (`FILTRES[nom]`).
# 
# Run:
# `~/MoodiMOS-IA/venv-cerveau/bin/python brain/robot_voice.py ["phrase to say"]`
# 
# Press a voice number to play, `t` for all, `q` to quit.
# 
# Piper (GPL-3.0) runs as a separate program; FFmpeg applies effects.

import os, subprocess, sys, tempfile

IA = os.environ.get("MOODIMOS_IA", os.path.expanduser("~/MoodiMOS-IA"))
PIPER = os.path.join(IA, "venv-cerveau", "bin", "piper")
VOIX = os.path.join(IA, "piper", "fr_FR-siwis-medium.onnx")
PHRASE = ("Bonjour, je suis Moudi Moss, ton petit robot de bureau. Il est quatorze heures, "
          "et je suis vraiment content de te voir !")
TAUX = 22050                                     # sampling frequency of the voice SiWiS


def hauteur(f):
    'Change voice pitch without altering speech rate (f > 1: higher-pitched).'
    return "asetrate={:.0f},aresample={},atempo={:.4f}".format(TAUX * f, TAUX, 1 / f)


# nom → FFmpeg filter chain; limiter prevents saturation caused by echo and flanger effects
FILTRES = {
    "originale (siwis)": "anull",
    "robot doux": hauteur(1.08) + ",aecho=0.8:0.7:7:0.4",
    "robot métallique": "aecho=0.8:0.8:5|11:0.5|0.3,tremolo=f=60:d=0.35",
    "petit robot mignon": hauteur(1.22) + ",flanger=delay=2:depth=2:regen=30:speed=0.5,aecho=0.8:0.6:6:0.3",
    "robot rétro, années 80": "tremolo=f=35:d=0.8,aecho=0.8:0.7:9:0.45,highpass=f=200,lowpass=f=5000",
    "robot sage, plus grave": hauteur(0.9) + ",aecho=0.8:0.7:8:0.4,chorus=0.7:0.9:25:0.3:0.3:2",
}


def robotiser(source, cible, filtre):
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", source, "-af", filtre + ",alimiter=limit=0.9",
                    cible], check=True)


def main():
    phrase = sys.argv[1] if len(sys.argv) > 1 else PHRASE
    noms = list(FILTRES)
    with tempfile.TemporaryDirectory() as tmp:
        brut = os.path.join(tmp, "brut.wav")
        subprocess.run([PIPER, "-m", VOIX, "-f", brut], input=phrase.encode(), check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        sons = []
        for i, nom in enumerate(noms):
            sons.append(os.path.join(tmp, "voix{}.wav".format(i + 1)))
            robotiser(brut, sons[-1], FILTRES[nom])
        print("Phrase : « {} »\n".format(phrase))
        for i, nom in enumerate(noms):
            print("  {}. {}".format(i + 1, nom))
        while True:
            choix = input("\nNuméro à écouter, t pour toutes, q pour quitter : ").strip().lower()
            if choix == "q":
                break
            liste = range(len(noms)) if choix == "t" else [int(choix) - 1] if choix.isdigit() else []
            for i in liste:
                if 0 <= i < len(noms):
                    print("  ▶ {}. {}".format(i + 1, noms[i]))
                    subprocess.run(["afplay", sons[i]])


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print()
