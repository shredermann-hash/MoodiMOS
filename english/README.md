# MoodiMOS — English build package

Start with the guide on Instructables. This folder is a standalone copy of the files needed
to build the USB-powered robot: both ESP32-S3 programs, printable parts, CAD generators,
Fusion scripts, wiring diagrams and optional Mac conversation tools.

The build documentation and file names are in English. **The physical robot still speaks and
understands French.** Its displayed words, game questions, voice models, serial commands and
French-language child safeguard retain their original meaning. Technical identifiers and personal
configuration keys are not a translated user interface.

## Before you start

MoodiMOS is a personal project shared free of charge. Claude helped develop it and wrote the
conversation safeguard from the author's rules; Codex helped review the code and documentation.
The files describe the author's build and tests and are provided as is, without warranty. Check
dimensions against your components and check the wiring before the first power-up.

Keep small parts and magnets away from young children. Conversation with AI is optional and
experimental; it may give incorrect or unsuitable answers. If a child uses it, enable the
French-language safeguard described in Step 15 and stay with them. It cannot guarantee every answer.

The supplied newer body has not been assembled with the board; the author's robot uses its front
panel on an earlier shell. Test the screen fit before printing the full body, as described in Step 3.

## Find the files

| Task | Location |
|---|---|
| Complete illustrated guide | the guide on Instructables |
| Neck contact map and wire colours | [MoodiMOS_Neck_Pogo_Wiring.md](MoodiMOS_Neck_Pogo_Wiring.md) |
| Overall wiring diagram | [MoodiMOS_Wiring_torso_head.png](MoodiMOS_Wiring_torso_head.png) |
| Neck wiring diagram | [MoodiMOS_Wiring_neck_pogo.png](MoodiMOS_Wiring_neck_pogo.png) |
| Print-ready parts | `cad/MoodiMOS_Robot/print_stl/` |
| CAD, dimensions and the two Fusion workflows | [cad/MoodiMOS_Robot/README.md](cad/MoodiMOS_Robot/README.md) |
| Printing settings | [cad/MoodiMOS_Organic/print_stl/PRINT_SETTINGS.md](cad/MoodiMOS_Organic/print_stl/PRINT_SETTINGS.md) |
| Head firmware | `firmware/moodimos-head/` |
| Torso firmware | `firmware/moodimos-torso/` |
| Behaviour and game | [extras/README.md](extras/README.md) |
| Optional conversation, announcements and timer | [brain/README.md](brain/README.md) |
| Design illustrations and English screen previews (the robot itself shows French) | `illustrations/` |
| Original-to-English path correspondence | [FILE_MAP.json](FILE_MAP.json) |
| Software checks and their limits | [VALIDATION.md](VALIDATION.md) |

Run commands from this folder, which contains `firmware/`, `cad/`, `brain/` and `extras/`.
Do not mix its renamed files with files from the French package. Keep the folder tree intact:
Python generators and Fusion scripts find their dependencies relative to their own files.

## Program the two boards

The reference toolchain is ESP32 3.3.11 and GFX Library for Arduino 1.6.7. Install it as described in
Step 5, then use the actual USB port shown by `arduino-cli board list`:

```sh
sh firmware/flash.sh torso /dev/cu.usbmodem1101
sh firmware/flash.sh head /dev/cu.usbmodem1101
```

Program each board separately for the first upload. Keep the head off the neck while using its
own USB port. After both boards are programmed and the neck works, update the closed head through
the torso on macOS:

```sh
sh firmware/flash_head_neck.sh /dev/cu.usbmodem1101
```

`head` and `torso` are English aliases for the flashing scripts; the old board arguments are also
accepted. The serial firmware commands remain French, exactly as shown in the guide. A compilation
check without uploading is available with `sh firmware/build.sh`.

## Regenerate the CAD

```sh
python3 -m venv cad/MoodiMOS_Organic/.venv
cad/MoodiMOS_Organic/.venv/bin/pip install -r cad/MoodiMOS_Organic/requirements.txt
cad/MoodiMOS_Organic/.venv/bin/python cad/MoodiMOS_Robot/robot_parts.py
cad/MoodiMOS_Organic/.venv/bin/python cad/MoodiMOS_Robot/robot_limbs.py
cad/MoodiMOS_Organic/.venv/bin/python cad/MoodiMOS_Robot/robot_verify.py
```

Wait for the generators to finish before importing into Fusion or rendering. They write parts placed
in assembly coordinates to `stl/`; `print_stl/` contains parts placed on the print bed. The supplied
STLs can be printed without Fusion. The `arm_*` and `sneaker_*` files in `print_stl/` are reduced to stay
under Instructables' 10 MB file limit (measured deviation: 0.02 mm at most); the generators write the
full files, up to 29 MB. For the optional base and painting aids, use `robot_base.py` and
`robot_stencils.py` in the same CAD folder.

For Fusion, add one of these folders through **Utilities > Add-Ins > Scripts and Add-Ins**:

- `cad/MoodiMOS_Robot/MoodiMOS_Robot/`: builds head and torso solids and imports sculpted limb meshes.
- `cad/MoodiMOS_Robot/MoodiMOS_Assembly/`: imports the generated mesh assembly.

Both require the generated files. Run these scripts inside Fusion, not with the terminal Python.
Local edits in Fusion do not automatically update the Python-generated STLs.

## Optional Mac conversation

Follow Step 15 to install Whisper, Ollama and Piper. Large models and the Python environment remain
outside this package, under `~/MoodiMOS-IA` by default. The existing name `venv-cerveau` and the
`MOODIMOS_*` environment variables are retained for compatibility with the French installation.

```sh
sh brain/mac_service.sh install
sh brain/mac_service.sh status
sh brain/mac_service.sh log
sh brain/mac_service.sh remove
```

No service is installed simply by opening this folder. These commands change the same MoodiMOS
conversation service as the French scripts: use one installation at a time. The robot must be
connected by a USB data cable. The Mac processes conversation locally after the required downloads.

Create `brain/people.json` yourself when configuring the safeguard or household profiles. Its keys,
such as `sans_nom`, `consigne`, `enfant`, `personnes` and `minuteur`, stay in French because they are
part of the configuration format. Keep the model's instructions and speech recognition in French.
Do not publish this personal file or `firmware/moodimos-torso/Household_local.h`.

## Torso voice transfer and useful serial commands

Commands are terminated by a newline. Firmware replies are in French. Close the conversation
service's use of the serial port before a manual session, as described in the guide.

| Command | Meaning |
|---|---|
| `capteurs` | Show room sensor readings |
| `etat` | Show status |
| `radar` | Show radar and dock detection |
| `camera` | Show camera tracking status, without exporting images |
| `tete clin 2` | Ask the head for a two-second wink |
| `silence` | Stop playback |
| `acces` / `acces fin` | Read or close physical command authorisation |

`dire name` plays a stored phrase. `deposer name length` stores WAV bytes, followed by the exact
announced byte count; use the supplied transfer tool rather than typing raw file bytes manually.
`dis length` and `dis+ length` stream 16 kHz signed 16-bit mono audio to the speaker; the latter
continues a response. `annonce length` requests an announcement transfer. See Step 12 for the
complete transfer procedure and expected firmware responses.

## Publication files

MoodiMOS (code, CAD, printable parts, diagrams and documentation) is released under the Creative Commons
Attribution-NonCommercial-ShareAlike 4.0 International licence (CC BY-NC-SA 4.0):
https://creativecommons.org/licenses/by-nc-sa/4.0/
This package excludes private household files and the Git history. Design renderings illustrate
the intended appearance; the assembled robot photographs and build notes describe what was
actually tested. Do not treat an image rendering as a fit check or an electronics test.
