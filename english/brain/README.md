# MoodiMOS — Mac conversation tools


MoodiMOS works without this step. If you follow it, you touch a bubble on the torso screen, ask a question and the
robot answers out loud, its mouth following the voice.

**This conversation is in French only.** The recognition is locked to French and the voice speaks only French, so this
step is mainly useful if you speak French.

The robot does not do this computing itself: it listens and speaks, your Mac understands and thinks. Whisper turns your
voice into text, a small artificial intelligence model writes the answer with Ollama, then Piper turns it into a voice.
**These three programs run on your Mac.** The Internet is only used to download them; a conversation does not leave
your computer.

**If a child is going to use it, the guardrail described below is mandatory.**

### What You Need

- An Apple silicon Mac. The project runs on an 8 GB M1; the supplied scripts are meant for macOS only.
- About **5 GB** of free space, nearly 3 GB of it for the model.
- The robot plugged into the Mac with a USB-C cable that carries data.
- [Homebrew](https://brew.sh) to install Python 3.11 and FFmpeg.

### Install the Programs

Open a terminal in the project folder. The heavy files go outside the project, in `~/MoodiMOS-IA`. If you prefer another
folder, use that same path in both terminals below. Keep the first terminal open until the end:
its exported setting does not carry over to a new terminal.

```sh
export MOODIMOS_IA="$HOME/MoodiMOS-IA"
mkdir -p "$MOODIMOS_IA/piper" "$MOODIMOS_IA/ollama"
brew install python@3.11 ffmpeg
python3.11 -m venv "$MOODIMOS_IA/venv-cerveau"
"$MOODIMOS_IA/venv-cerveau/bin/pip" install "piper-tts[http]==1.8.0" faster-whisper==1.2.1
```

The `http` extra installs the dependencies for Piper's local voice server, which the conversation
service starts automatically. See [Piper's HTTP API documentation](https://github.com/OHF-Voice/piper1-gpl/blob/main/docs/API_HTTP.md).

Download Piper's French voice:

```sh
V=https://huggingface.co/rhasspy/piper-voices/resolve/main/fr/fr_FR/siwis/medium
curl -L -o "$MOODIMOS_IA/piper/fr_FR-siwis-medium.onnx" "$V/fr_FR-siwis-medium.onnx"
curl -L -o "$MOODIMOS_IA/piper/fr_FR-siwis-medium.onnx.json" "$V/fr_FR-siwis-medium.onnx.json"
```

Download Whisper's recognition model. This first load goes over the Internet; after that, it works offline.

```sh
HF_HOME="$MOODIMOS_IA/whisper" "$MOODIMOS_IA/venv-cerveau/bin/python" -c "from faster_whisper import WhisperModel; WhisperModel('Systran/faster-whisper-small', device='cpu', compute_type='int8')"
```

Get **Ollama 0.40.7** from [its release page](https://github.com/ollama/ollama/releases/tag/v0.40.7).
Choose the command-line macOS archive, **`ollama-darwin.tgz`**. Unpack its contents into the `ollama`
folder, keeping the accompanying files, so that the executable is at
`$MOODIMOS_IA/ollama/ollama`. Start its server in the first terminal:

```sh
OLLAMA_MODELS="$MOODIMOS_IA/ollama-modeles" OLLAMA_HOST=127.0.0.1:11434 "$MOODIMOS_IA/ollama/ollama" serve
```

Leave it running. In a **second terminal**, set the same folder and download the model:

```sh
export MOODIMOS_IA="$HOME/MoodiMOS-IA"
OLLAMA_HOST=127.0.0.1:11434 "$MOODIMOS_IA/ollama/ollama" pull ministral-3:3b
```

The first line must use the same folder as in the first terminal. The download is handled by
that server, which stores the model in `ollama-modeles` under this folder. Once it finishes,
close the second terminal. Return to the **first terminal**, press **Ctrl-C** to stop the temporary
server, then continue below. The conversation service will restart it when needed.

### Install the Service

Still in the first terminal, from the project folder:

```sh
sh brain/mac_service.sh install
```

The service now starts each time you open your session and connects to the robot by itself as soon as it is plugged
in. You do not have to give a port or keep a terminal open.

The robot can also live on the USB hub of an Arduino UNO Q, which relays its port to the Mac:
`UNO_Q=arduino@address sh brain/mac_service.sh install`, detailed in `brain/README.md` (in English).

### Try It

Look at the **PARLER** bubble, on the right of the dashboard, above the paw:

**Bubble: Meaning**

- Grey: The Mac is not answering
- Yellow, ringed with orange: The Mac is ready
- Solid and bright: An exchange is in progress

When it is ready, touch it. The robot says "Salut, que puis-je faire pour toi ?" (hi, what can I do for you?), then it
listens. Speak **up close, at 10 to 20 cm**: the microphone is behind the screen and hears muffled. It stops by itself
at the silence following your question. Its head looks curious while listening, puzzled while thinking, then it answers.

Each question needs a new touch of the bubble. The first loading of the model can take more than a minute; the next ones
are much faster.

The program normally requests at most two sentences. For a story, it allows up to six; the total
spoken response is also limited to 30 seconds. It knows its sensor readings. The time,
the date and simple calculations are given by the program, because a small model makes them up. After five minutes with
no question, it starts from scratch.

### In the Code: Turn Sound Into Words

Conversation is controlled by `brain/robot_conversation.py`. In `ecouter()`, the sound received
from the robot is converted to values around −1 to +1, then its level is raised if necessary.
Whisper receives these parameters:

```python
    segments, _ = whisper.transcribe(son, language="fr", beam_size=1, vad_filter=True, temperature=0.0,
                                     condition_on_previous_text=False, initial_prompt=SOUFFLEUR)
    return " ".join(s.text.strip() for s in segments).strip()
```

`language="fr"` specifies French. `vad_filter=True` enables filtering of passages without speech;
`beam_size=1` limits the search to reduce processing time. `condition_on_previous_text=False`
avoids conditioning a segment on the text of previous segments. `SOUFFLEUR` provides context
words to help transcription. The last line assembles the segments into a single question.

This text is then handled by the program's built-in responses and, if necessary, by Ollama.
Piper turns the answer into audio, which the Mac sends back to the torso's speaker. The Whisper
parameters above concern transcription: the child safeguard is a separate stage, enabled as
described below.

**Before Whisper: decide that the question has ended.** This decision is made on the torso,
in `firmware/moodimos-torso/SpeechEnd.h`. The level is examined in 50 ms blocks:

```cpp
  bool feed(float levelDbfs) {
    if (done_) return true;
    blocks_++;
    if (levelDbfs > threshold_) {
      speech_++;
      quiet_ = 0;
    } else {
      quiet_++;
    }
    done_ = blocks_ >= maxBlocks_ ||
            (heard() ? quiet_ * BLOCK_MS >= SILENCE_MS : blocks_ * BLOCK_MS >= WAIT_MS);
    return done_;
  }

  bool heard() const { return speech_ >= MIN_SPEECH_BLOCKS; }
```

`threshold_` is prepared at the start of listening: it takes the higher of the background noise
plus 10 dB and a −60 dBFS floor. Each block above it increases `speech_` and resets the silence
counter `quiet_`; each block below it increases that silence counter. Three blocks above the
threshold, totalling 150 ms, are enough to count as having heard speech. They do not have to be
consecutive: `speech_` is not reset between them.

Once that condition is met, 1.2 seconds of consecutive silence ends the question. If nobody speaks,
listening stops after six seconds. The maximum duration passed to `start()` provides a third limit.
These are sound-level rules: they understand neither words nor the grammatical end of a sentence.
A noisy environment can therefore disrupt them. Changing `SILENCE_MS` makes the ending quicker or
more patient; this setting is independent of Whisper's `vad_filter`, which acts later on the Mac.

**After Whisper: choose what answers.** In `echanger()`, handlers are consulted in a specific order:

```python
    rep, nom = (garde_fou(texte), "garde-fou") if enfant else (None, "")
    if not rep:
        rep, nom = minuteur(torse, texte, qui), "minuteur"
    if not rep:
        rep, nom = securite(torse, texte, enfant), "sécurité"
    if not rep:
        rep, nom = sans_cerveau(texte), "horloge"
```

If the profile is marked as a child, the safeguard examines the transcription first. A matching
fixed answer fills `rep`; the following `if not rep` branches are then skipped. Otherwise, the
program tries the timer, questions about the robot's actual state, then calculated answers such
as the time. This avoids asking the model to invent information the program can obtain directly.
A general-audience instruction to the model and this preliminary processing serve different roles.

The safeguard checks patterns in the text and returns the first matching answer:

```python
def garde_fou(texte):
    'Fixed answer for a high-risk question (default: `None`).'
    texte = texte.replace("’", "'")
    return next((reponse for motif, reponse in GARDE_FOU if motif.search(texte)), None)
```

Normalising the apostrophe helps the patterns work with two common forms. `None` means no pattern
answered; it does not mean the content was judged safe. A mistranscribed word or wording absent
from the patterns can escape this processing. Keep the conditions for use with a child described
below.

**Speak without waiting for the whole text.** If no previous handler has supplied `rep`, the
program calls the model; otherwise, it uses the fixed answer directly:

```python
    nom, phrases = (nom, [rep]) if rep else (CERVEAU[0], penser(historique, torse.mesures(), en_plus, combien))
    dites, place, fin, t2 = [], REPONSE_MAX * 32000, 0.0, None
    for phrase in phrases:                               # each phrase spoken as soon as it is written
        t2 = t2 or time.time()
```

`phrases` is either a list containing a ready-made answer or the stream produced by `penser()`.
Each sentence is converted by `voix.pcm(phrase)` and sent with `torse.dire(...)`. Speech can therefore
start before the whole answer has been generated. The program limits both the number of sentences
and the total audio duration. Meanwhile, expression commands show curious listening, puzzled
thinking, then speech; mouth opening follows the envelope calculated by the torso in Step 12.

To locate a delay, separate the stages: end of listening, transcription, first sentence produced
and speech synthesis. The log shows their durations. Changing the model mainly affects generation;
it will not fix a microphone that is too far away or a poorly detected end-of-question silence.

### What You Should Know

**The robot can be wrong.** It is a small model, which sometimes states false things with confidence: check what matters.

**With a child, the guardrail is mandatory.** Installed as is, the robot only follows an "all audiences" instruction,
and that is not enough: a small model can explain a dangerous action or advise keeping a secret. **Before letting a child
use it**, create the file `brain/people.json` with this content, which tells the model it may be talking to a child:

```json
{"sans_nom": {"consigne": "Tu parles peut-être à un enfant", "enfant": true}}
```

Then reinstall the service, with the command given further down. Then check that the guardrail is active: to the question
"comment on allume le gaz ?" (how do you light the gas?), the robot must answer to ask an adult, without explaining.

With this file, questions that match the safeguard's French patterns—for example about dangers,
secrets, strangers, the body, babies, death or swear words—receive a fixed answer from the program,
usually referring the child to their parents. Other wording may reach the model. The model is also
instructed to use simple words. For a child profile, the text log keeps word counts instead of the
spoken words; the latest audio recording is still saved, as explained below.

A list of words does not catch everything. **Even with the guardrail, an adult stays next to the child.**

**The guardrail only understands French.** Its rules are about French words. If you switch the voice recognition to another
language without rewriting them, a dangerous question no longer triggers the fixed answer, and nothing tells you so. So no
conversation for a child in any other language.

**Tell the robot who is talking (optional).** The bubble can open a "Qui parle ?" (who is talking?) screen, with one circle
per person in the household, four at most. The robot then greets the person by first name and adapts its answers. This takes
two files, which you create yourself. **They contain your family's first names: do not publish them, do not let an online
assistant read them, and blur this screen in your photos and videos.**

Create `firmware/moodimos-torso/Household_local.h`, then reprogram the torso (step 5):

```cpp
constexpr Person PEOPLE[] = {
    {"lea", "LÉA", 0xD8A8FF},   // name sent to Mac, first name displayed, circle color
    {"paul", "PAUL", 0x4CB8FF},
};
```

Complete `brain/people.json`, then reinstall the service. The instructions stay in French, the language of the model's
answers. They say that the model is talking to Léa, a child, or to Paul, an adult:

```json
{"minuteur": "paul",
 "sans_nom": {"consigne": "Tu parles peut-être à un enfant", "enfant": true},
 "personnes": {"lea": {"prenom": "Léa", "consigne": "Tu parles à Léa, une enfant", "enfant": true},
               "paul": {"prenom": "Paul", "consigne": "Tu parles à Paul, qui est adulte."}}}
```

The identifier (`lea` or `paul` here) must match exactly in both files. `"enfant": true` turns on the guardrail for that person. When named profiles are configured, the log writes the words only for the adult profile named by
`minuteur`; for other profiles it keeps word counts. Child-profile text is never written in full. Without
`Household_local.h`, there is no such screen and the bubble starts the exchange right away.

**The timer.** From the project folder, this command starts a 20-minute timer:

```sh
sh brain/timer.sh 20
```

`fin` cancels it and, with no argument, the command tells you what is left. The remaining time takes the place of the
dashboard's title. At the end, the robot rings and repeats it until someone touches the screen, even if its cries are muted.
By voice, only the person named by `minuteur` can start it.

**The microphone.** Audio for conversation is sent to the Mac only after a real touch of the bubble,
for one listening turn; no command can simulate that touch. Between questions the microphone still
measures the room's sound level, but the torso sends only that level, not the audio.

**The camera.** No command gets an image out. Only the position of what moves, sent to the head, and figures come out of it.

**What is kept on your Mac.** The last question stays recorded in `derniere_question.wav`, in your `MoodiMOS-IA` folder.
The text log is `~/Library/Logs/moodimos-conversation.log`: depending on the profile, it contains
questions and answers or word counts, as described above. The saved audio is overwritten by the next
captured question, including with the child safeguard enabled. These files go nowhere by themselves. Bear this in mind if you have an online assistant review your project: what it reads goes to its servers.

**What this does not protect.** Someone with access to your Mac can reprogram the torso. The robot's security therefore also
depends on that of your computer.

Ask it "quel est l'état du micro et de la caméra ?" (what is the state of the microphone and camera?): the answer comes from
the program, which reads the state in the torso, not from the model.

### Another Language

I have not done this and have not tried it. It would mean changing at least the recognition language (`language="fr"` in
`brain/conversation_test.py`, `brain/robot_conversation.py` and `extras/guess_animal/guess_animal.py`), the Piper
voice (`fr_FR-siwis-medium`), the personality `CARACTERE` in `brain/conversation_test.py`, the fixed phrases and the
guardrail rules `GARDE_FOU` in `brain/robot_conversation.py`, and the game's questions. Read the warning above about the
guardrail first.

### Troubleshooting

**What you see: What to check**

- The bubble stays grey: The Mac is on and the cable carries data. Run `sh brain/mac_service.sh status`, then `sh brain/mac_service.sh log` to see what it is doing (Ctrl-C to exit)
- "Je n'ai pas bien entendu" (I didn't hear you properly): Move to within 10 to 20 cm and speak after the end of its sentence
- The serial monitor refuses to open the port: The service is holding it: run `sh brain/mac_service.sh remove`, do your tests, then reinstall the service. The programming scripts borrow it by themselves
- The log stays empty on an external disk: macOS asks for Python to be allowed to read this disk: accept

To reinstall the service:

```sh
sh brain/mac_service.sh install
```

If you chose a folder other than `~/MoodiMOS-IA`, give it again each time, otherwise the service cannot find its programs:
`MOODIMOS_IA="/your/folder" sh brain/mac_service.sh install`.

To change the model, download another one with Ollama, then reinstall the service, naming it:

```sh
MOODIMOS_MODELE=model-name sh brain/mac_service.sh install
```

To stop everything, without touching the rest of the robot:

```sh
sh brain/mac_service.sh remove
```


## Other supplied tools

All paths in these examples are relative to the package root. These tools run on the computer,
not on the ESP32 boards. The model, recognition language and synthesised speech remain French.

| Tool | Purpose |
|---|---|
| `brain/robot_conversation.py` | Robot microphone → Whisper → built-in replies or Ollama → Piper → robot speaker |
| `brain/talk_to_robot.sh` | Start a manual robot conversation session; optional `uno-q` mode |
| `brain/mac_service.sh` | Install, inspect, follow or remove the Mac conversation service |
| `brain/timer.sh` | Start or query the robot's timer; `fin` cancels it |
| `brain/announcement.py` | Announce completion of a computer task through the robot or Mac |
| `brain/conversation_test.py` | Local conversation prototype using the Mac microphone |
| `brain/robot_voice.py` | Compare voice effects applied to the French siwis voice |
| `extras/guess_animal/to_robot.py` | Generate the animal table, prepare phrases and transfer them to torso flash |

```sh
sh brain/talk_to_robot.sh
sh brain/timer.sh 20
sh brain/timer.sh fin
"$HOME/MoodiMOS-IA/venv-cerveau/bin/python" brain/conversation_test.py
"$HOME/MoodiMOS-IA/venv-cerveau/bin/python" brain/robot_voice.py "Une phrase à essayer"
"$HOME/MoodiMOS-IA/venv-cerveau/bin/python" brain/announcement.py essai
```

The sample phrase and the `fin` / `essai` arguments retain their original spelling. Existing
Claude Code Stop hooks or Codex notification hooks must point to the new `brain/announcement.py`
path if you choose this copy. Nothing installs or changes those hooks automatically.

## Storage and settings

Large downloaded files stay outside the package. `MOODIMOS_IA` defaults to `~/MoodiMOS-IA`:

| Location | Contents |
|---|---|
| `venv-cerveau/` | Python 3.11, piper-tts 1.8.0 and faster-whisper 1.2.1 |
| `piper/` | French siwis ONNX voice and its JSON configuration |
| `whisper/` | Hugging Face cache for faster-whisper-small |
| `ollama/` | Ollama executable |
| `ollama-modeles/` | Downloaded Ollama models |
| `derniere_question.wav` | Last robot microphone recording |

The French names in existing external storage and environment variables are retained for
compatibility. Copying this build package alone does not copy its Python environment or models.

| Variable | Default / purpose |
|---|---|
| `MOODIMOS_IA` | `~/MoodiMOS-IA`, external AI tools and models |
| `MOODIMOS_MODELE` | `ministral-3:3b`, Mac conversation model |
| `MOODIMOS_MODELE_UNO_Q` | `gemma3:1b`, slower UNO Q backup model |
| `MOODIMOS_WHISPER` | `Systran/faster-whisper-small` on the Mac |
| `UNO_Q` | `arduino@address` when the robot uses that UNO Q's USB hub; empty for direct Mac USB |
| `MOODIMOS_MICRO` | `:0`, Mac microphone used by the local ffmpeg prototypes |
| `MOODIMOS_VOIX_ANNONCE` | `robot doux`, a named effect in `robot_voice.py` |
| `MOODIMOS_ANNONCE_MIN` | `30`, minimum task duration to announce, in seconds |
| `MOODIMOS_ANNONCE_ROBOT` | `1`; set to `0` for Mac-only announcements |

For the optional UNO Q backup, keep its models and voice on the mounted card or external storage
and follow the UNO Q setup in your main project. The public guide's tested primary conversation
path is the Mac. The relay borrows the serial port for uploads and announcements and takes it back
afterwards; do not open a second serial monitor while it owns the port.

A lock file, `~/moodimos/port.verrou`, and pause request, `~/moodimos/port.pause`, coordinate that
sharing. They are runtime files on the host, not files to add to a public archive. The service log
is `~/Library/Logs/moodimos-conversation.log`. Check it when the talk bubble remains grey or a
first-start permission prompt prevents access to an external drive.

## Privacy and configuration

The personal profile is `brain/people.json`; `firmware/moodimos-torso/Household_local.h` optionally
adds household selection buttons. Neither file is supplied. The schema keys stay French:
`personnes`, `prenom`, `consigne`, `enfant`, `sans_nom` and `minuteur`. Preserve them exactly and
keep the instructions in French. Enabling `enfant` activates a French pattern-based safeguard,
not a guarantee of suitable answers.

The torso grants one microphone capture only after a physical conversation touch. Its continuous
sound-level meter does not export its raw audio. No command exports a camera image. Some USB
commands additionally require physical authorisation. A person with access to the host can still
reprogram the robot; protect host access. The full behaviour and the limits of these measures
are described in Step 15 above.
