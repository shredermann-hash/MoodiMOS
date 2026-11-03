# Run a command via the torso (torso) port alone (`firmware/flash.sh`, `flash_head_neck.sh`). Robot connected to this Mac, ongoing conversation (`brain/robot_conversation.py`): its relay holds the port, releases it to the process writing its PID in `~/moodimos/port.pause` and reclaims it once the lock is free, at the end of the command.
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
            sys.exit('Another program is using the torso port')
        with open(pause, "w") as f:
            f.write(str(os.getpid()))
        time.sleep(0.1)
if os.path.exists(pause):
    os.remove(pause)
sys.exit(subprocess.call(sys.argv[1:], env=dict(os.environ, MOODIMOS_PORT_PRIS="1")))
