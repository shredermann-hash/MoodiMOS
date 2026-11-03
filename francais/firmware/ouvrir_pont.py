#!/usr/bin/env python3
# Ouvre le pont du cou sur le torse qui vient de redémarrer : attente de « Tête en place », puis « pont »
# et attente de « pont prêt ». Sans pyserial (termios de Python), sur le Mac comme sur la UNO Q.
#   python3 firmware/ouvrir_pont.py <port du torse>
# L'USB du torse disparaît un instant au redémarrage.
import os, select, sys, termios, time

def ouvrir(limite):
    while time.time() < limite:
        try:
            fd = os.open(sys.argv[1], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        except OSError:
            time.sleep(0.2)
            continue
        attr = termios.tcgetattr(fd)
        attr[0] = attr[1] = attr[3] = 0
        attr[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attr[4] = attr[5] = termios.B115200
        termios.tcsetattr(fd, termios.TCSANOW, attr)
        return fd
    sys.exit("le torse n'est pas revenu sur l'USB après son redémarrage")

def attendre(fd, fin, limite):
    seen = b""
    while time.time() < limite and fin not in seen:
        try:
            if select.select([fd], [], [], 0.2)[0]:
                bloc = os.read(fd, 4096)
                if not bloc:
                    raise OSError("port fermé")
                seen += bloc
        except BlockingIOError:
            pass
        except OSError:   # port encore celui d'avant le redémarrage
            try:
                os.close(fd)
            except OSError:
                pass
            time.sleep(0.2)
            fd = ouvrir(limite)
    return fd

time.sleep(1)
limite = time.time() + 15
fd = attendre(ouvrir(limite), b"te en place", limite)   # « Tête en place » : le pont peut s'ouvrir
os.write(fd, b"pont\n")
seen, t0, attente = b"", time.time(), 8
while time.time() - t0 < attente:
    if select.select([fd], [], [], 0.2)[0]:
        try:
            seen += os.read(fd, 4096)
        except BlockingIOError:
            pass
        if b"pont pr" in seen:
            os.close(fd)
            sys.exit(0)
        if b"pont : " in seen:
            break
        if b"refus" in seen:                             # accès fermé : il attend le geste sur l'écran du torse
            if attente == 8:
                sys.stderr.write("Autorise le pont : appui long sur l'écran du robot (60 s au plus).\n")
            attente, seen = 60, b""
            time.sleep(1)
            os.write(fd, b"pont\n")
sys.stderr.write(seen.decode("utf-8", "replace"))
sys.exit("le torse n'a pas ouvert le pont (tête posée ? firmware du torse à jour ?)")
