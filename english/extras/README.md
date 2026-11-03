# MoodiMOS — behaviour, game and screens

This is the edition described in the public build guide. It combines the orange, ivory and black
body with an amber neon face on a black background. For the complete behaviour, gestures, game,
firmware explanations and closed-head update procedure, see **Step 14** of
the guide on Instructables. Its CAD is in `../cad/MoodiMOS_Robot/`.

## Interaction design

Gentle gestures should be more rewarding than force. A tap calms the character, rubbing or three
rapid taps makes it laugh, and a stationary long press produces a kiss and hearts. Repeated
jostling increases anger, which fades when left alone. No reaction requires forceful tapping or
shaking. Working quietly nearby does not make the robot sulk; abandoning a game can.

| Feature | Behaviour |
|---|---|
| Default face | Amber/yellow glass eyes, orange brows and mouth, pink cheek strokes, neon glow |
| Moods | Colour changes on the facial features; the background stays black |
| Sleep | Soft mauve features, small smile, pink cheeks and rising z's |
| Double tap | Pocket-watch view: eyes look towards the time, displayed where the mouth was |
| Tickle | Three or more rapid taps, or rubbing either touchscreen; laughter and bouncing |
| Kiss | Stationary long press on either screen; closed eyes, round mouth and pink hearts |
| Design menu | Hold the head screen still for 3 seconds, then swipe left/right |
| Available designs | Neon, classic, clock and full-body clock |
| Torso dashboard | Clock, five ambient readings, presence/head state, camera indicator, talk/game/sound buttons; a Wi-Fi icon between the title and the camera indicator, yellow once the time comes from the network, grey otherwise, absent with no Wi-Fi saved; "USB" in yellow when the torso is plugged in |
| Sleep dashboard | Sleeping face; touching the torso shows its readings for 30 seconds |
| Timer | Countdown replaces the dashboard title; ringing continues until a screen touch |

The head has no battery in this USB build and stays on the magnetic neck. Detachment-related code
exists for other configurations, but removal is not an everyday interaction in this guide.

## Guess the Animal

The game runs on the robot without the Mac or a language model. It uses 38 animals and weighted
probabilities. Answer using yes, no or unknown on the torso; three guesses are allowed. Questions
are spoken from recorded phrases in torso flash. Without an answer, a question is repeated twice
before the game is abandoned. The close button exits without sulking. Darkness does not put the
head to sleep during a game.

Edit `guess_animal/animals.json`, then use `guess_animal/to_robot.py` with `table`, `voix` and
`envoyer` in that order; reprogram the torso if its table changed. The final two arguments retain
their French spelling. Piper prepares the French phrases; the transfer tool stores them by USB.
No memory card or opening of the robot is required. See Steps 12 and 14 for the exact commands.

`guess_animal/guess_animal.py` is the earlier Mac prototype, with spoken answers through Whisper.
The JSON's questions, animal names and configuration keys remain French so that the physical
robot's game and voice data stay consistent.

## Program the robot

Use the same edition for both boards:

```sh
sh firmware/flash.sh torso /dev/cu.usbmodem1101
sh firmware/flash_head_neck.sh /dev/cu.usbmodem1101
```

These examples are run from the package root; replace the port. For the first head upload, use
`sh firmware/flash.sh head ...` with the head off its neck and on its own USB port. Closed-head
updates require both current firmwares and a verified neck link.

For a robot connected to an Arduino UNO Q USB hub, `firmware/flash_uno_q.sh` compiles on the Mac
and uploads through SSH to the UNO Q. Set `UNO_Q=arduino@address` as described in the guide.
Only application firmware is written; recorded phrases in the torso flash are retained. The
reference UNO Q esptool environment uses **esptool 5.3.1**; 5.6.0 did not work through the neck
in the recorded tests.

## Screen previews and sources

- `neon_face.png`: expressions rendered from the firmware's drawing code.
- `dashboard.png`: torso dashboard states.
- `dial.png`: double-tap clock face.
- `timer.png`: countdown and timer-finished screen.
- `who_is_speaking.png`: anonymised household-selection example.
- `clock/clock_preview.html`: interactive clock-face prototype; controls are in English.

Preview images show the robot's French interface. The main drawing code is in `Face.cpp`,
`DashboardNeon.cpp`, `Watch.cpp`, `Canvas.cpp` and `StrokeFont.h`. Head and torso share matching
protocol and drawing helpers. The English package changes the surrounding documentation and
file paths while retaining the robot's French speech, protocol and safeguard.
