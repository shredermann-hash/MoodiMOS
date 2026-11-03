#!/usr/bin/env python3
# Open the bridge on the torso after it restarts: wait for "Head in place", then "bridge" and wait for "bridge ready". Without PySerial (termios in Python), works on Mac and UNO Q.
# 
# Run:
# ```bash
# python3 firmware/open_bridge.py <torse_port>
# ```
# The torso USB disappears briefly on startup.
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
    sys.exit('The torso did not reappear on USB after restarting')

def attendre(fd, fin, limite):
    seen = b""
    while time.time() < limite and fin not in seen:
        try:
            if select.select([fd], [], [], 0.2)[0]:
                bloc = os.read(fd, 4096)
                if not bloc:
                    raise OSError('Port closed')
                seen += bloc
        except BlockingIOError:
            pass
        except OSError:   # Original state retained post-restart
            try:
                os.close(fd)
            except OSError:
                pass
            time.sleep(0.2)
            fd = ouvrir(limite)
    return fd

time.sleep(1)
limite = time.time() + 15
fd = attendre(ouvrir(limite), b"te en place", limite)   # « Head in place »: the bridge can be opened.
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
        if b"refus" in seen:                             # closed access: it waits for the gesture on the torso’s screen.
            if attente == 8:
                sys.stderr.write('Authorise the bridge: long-press the robot screen (60 seconds maximum).\n')
            attente, seen = 60, b""
            time.sleep(1)
            os.write(fd, b"pont\n")
sys.stderr.write(seen.decode("utf-8", "replace"))
sys.exit('The torso did not open the bridge (is the head attached and is the torso firmware up to date?)')
