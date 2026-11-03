# Lance une commande avec le port du torse pour elle seule (firmware/flash.sh, flash_tete_cou.sh). Robot branché sur
# ce Mac et conversation en route (cerveau/conversation_robot.py) : son relais tient le port, le rend à qui écrit son
# numéro de processus dans ~/moodimos/port.pause et le reprend quand le verrou est libre, à la fin de la commande.
import fcntl, os, subprocess, sys, time

dossier = os.path.expanduser("~/moodimos")
pause = os.path.join(dossier, "port.pause")
verrou = os.open(os.path.join(dossier, "port.verrou"), os.O_CREAT | os.O_RDWR, 0o644)
fin = time.time() + 10
while True:
    try:
        fcntl.flock(verrou, fcntl.LOCK_EX | fcntl.LOCK_NB)
        break
    except BlockingIOError:
        if time.time() > fin:
            if os.path.exists(pause):
                os.remove(pause)
            sys.exit("port du torse pris par un autre programme")
        with open(pause, "w") as f:
            f.write(str(os.getpid()))
        time.sleep(0.1)
if os.path.exists(pause):
    os.remove(pause)
sys.exit(subprocess.call(sys.argv[1:], env=dict(os.environ, MOODIMOS_PORT_PRIS="1")))
