# Cerveau : les outils du Mac

Ces scripts tournent sur le Mac, jamais dans le robot. Trois cas bien distincts :

1. **Déjà utiles au robot, sans IA.**
   - `cerveau/annonce.py` : annonce de fin de tâche (Claude Code, Codex). Le robot la dit quand son torse
     est branché en USB (commande `annonce` du firmware), sur le Mac ou sur le hub de la UNO Q (le son lui
     est passé par SSH, `UNO_Q=arduino@adresse` à l'installation du service), sinon les haut-parleurs du Mac.
   - `extras/devine_animal/vers_robot.py` : table et phrases du jeu « Devine l'animal », rangées dans
     la flash du torse. Une fois rangées, le robot joue seul, sans le Mac.
2. **Prototypes sur le Mac**, le robot n'y prend pas part.
   - `cerveau/essai_conversation.py` : conversation locale. Micro du Mac → Whisper → modèle d'Ollama → Piper.
   - `cerveau/voix_robot.py` : comparateur des six voix de robot (effets ffmpeg sur la voix siwis).
   - `extras/devine_animal/devine_animal.py` : le jeu avec réponses à la voix (Whisper), avant son
     passage dans le robot.
3. **Conversation par le robot**, branché en USB sur le Mac, ou sur le hub de la UNO Q (29/10/2026),
   `cerveau/conversation_robot.py` :
   micro du torse (commande `ecoute` : il s'arrête seul au silence qui suit la question) → Whisper → cerveau →
   Piper, voix siwis → haut-parleur du torse (commande `dis`, jouée dès le premier morceau reçu, bouche de la tête
   synchronisée). Chaque phrase est dite dès que le modèle l'a écrite, la suivante ajoutée sans coupure (`dis+`) :
   environ 1 à 2 s de gagnées. Une réponse tient en deux phrases ; une histoire demandée (« raconte-moi une
   histoire », « tu connais un conte ? », pas « l'histoire de France » ni une blague) en cinq ou six, 30 s de voix au
   plus, et la phrase qui n'y tient plus n'est pas dite plutôt que coupée (banc : 16 à 26 s). La tête est curieuse
   pendant l'écoute, perplexe pendant la réflexion. L'heure, la date (aujourd'hui,
   demain, hier, dans trois jours…) et les calculs simples (« combien font sept fois huit ? ») sont dits par le
   programme : un petit modèle invente l'heure, compte faux et se trompe de jour. Les outils (« tools ») de
   ministral-3:3b ne font pas mieux (30/10/2026, 56 questions : 14 à 35 justes selon la consigne, contre 42 et aucune
   fausse pour le programme ; poussé à s'en servir, il converse moins bien). Le cerveau reçoit les mesures des
   capteurs du torse. Sa consigne lui demande des faits exacts, sans comparaison, les nombres en chiffres, à la
   température 0,2 (banc du 30/10/2026, 12 questions posées 2 fois) : avant, il inventait des programmes télé, une
   araignée « à huit pattes comme un chat », des images qui embrouillaient, et se trompait sur Pythagore. Il reste
   quelques erreurs de petit modèle (« Madrid, capitale depuis le XIXe siècle »). Les formules sont dites en mots
   (« A = π × r² » : « A égale pi fois r au carré » ; « CO₂ » : « CO 2 »).
   - Par défaut, tout tourne sur le Mac (Whisper small, ministral-3:3b, Piper), robot branché en USB sur le Mac : le
     même script, en mode `relais`, tient le port série du torse. Robot sur le hub de la UNO Q
     (`UNO_Q=arduino@adresse sh cerveau/service_mac.sh installer`, gardée aux installations suivantes et reprise par
     `parler_au_robot.sh` ; `UNO_Q=` pour revenir au Mac) : le relais tourne sur la UNO Q, à travers SSH.
   - Mac éteint (`uno-q`), tout tourne sur la UNO Q, bien plus lentement : Whisper base, gemma3:1b.
   - Lancée par le bouton bulle de l'écran du torse quand le service du Mac tourne (`cerveau/service_mac.sh`, démarré
     à l'ouverture de la session). Tableau de bord classique : à droite de la température, grise quand le Mac ne
     répond pas, cerclée de la couleur d'accent quand il est prêt, pleine pendant l'échange. Édition complète : à droite du
     tableau de bord, grise, jaune, puis orange. Le Mac rappelle au torse toutes les 8 s qu'il est là ; sans
     nouvelles pendant 25 s, la bulle se grise. Il dit « Salut, que puis-je faire pour toi ? », puis écoute ; s'il
     n'a rien entendu ou rien compris, il le dit. Réponses tout public : la consigne dit qu'un enfant écoute
     peut-être (jamais de gros mot, rien qui fasse peur). La conversation repart de zéro après 5 min sans question.
   - La maison, dans deux fichiers personnels, gardés dans le dépôt mais hors de toute archive publiée
     (`.gitattributes`, `git archive`) : `firmware/moodimos-torse/Household_local.h`, jusqu'à quatre personnes (nom
     envoyé au Mac, prénom affiché, couleur ; exemple en tête de `Household.h`), et `cerveau/personnes.json`. Avec
     eux, en édition complète, l'appui ouvre l'écran « Qui parle ? » : un rond de couleur par personne, sa première lettre
     dedans et son prénom dessous ; sans choix, le tableau de bord revient au bout de 10 s, la
     croix le ferme. Le rond touché envoie `bouton parler <nom>` au Mac, qui dit « Salut <prénom>, … ». La consigne
     dit au modèle à qui il parle : un jeune enfant (mots simples, garde-fou), un adolescent (faits justes,
     mots du collège et du lycée), un adulte. Sans nom, il parle peut-être à un enfant. La conversation repart aussi
     de zéro quand quelqu'un d'autre parle.

     ```json
     {"minuteur": "paul",
      "sans_nom": {"consigne": "Tu parles peut-être à une petite fille de six ans", "enfant": true},
      "personnes": {"lea": {"prenom": "Léa", "consigne": "Tu parles à Léa, une petite fille de six ans", "enfant": true},
                    "paul": {"prenom": "Paul", "consigne": "Tu parles à Paul, qui est adulte."}}}
     ```

     `consigne` s'ajoute à celle du modèle ; `enfant` y ajoute, après « : », les règles d'un enfant (`ENFANT` de
     `conversation_robot.py`) et met le garde-fou ; `sans_nom` : la bulle sans écran ; `minuteur` : la seule personne qui peut s'en servir à la voix. Les prénoms aident aussi Whisper à les
     écrire.
   - Minuteur (édition complète), compris par le programme et non par le modèle, comme l'heure. Bulle, puis « mets un
     minuteur de 20 minutes » (aussi « une heure et demie », « 1h30 », « un quart d'heure », « rappelle-moi dans dix
     minutes »), « il reste combien ? », « annule le minuteur ». Une demande d'une autre personne reçoit une réponse
     fixe, sinon le modèle prétendrait l'avoir lancé ; sans `minuteur` dans `personnes.json`, personne ne s'en sert.
     Le décompte vit dans le torse : il sonne même si le Mac dort, jusqu'à ce qu'on touche l'écran. Sans la voix, `sh cerveau/minuteur.sh` ci-dessous.
   - Garde-fou des enfants (`enfant` de `personnes.json` ; banc du 30/10/2026) : ministral expliquait comment allumer le gaz,
     conseillait de garder un secret pour les parents, ne disait pas « non » à l'inconnu en voiture, définissait les
     gros mots et inventait le corps. Ces questions (gros mots, insultes, dangers, secret, inconnu, corps touché, corps,
     bébés, mort) ont une réponse fixe du programme, douce, qui renvoie à papa ou maman : 22 questions à risque
     reconnues, 20 innocentes laissées au modèle (« Jupiter est faite de gaz ? », « Je suis mort de rire »). Pour le
     reste, la consigne lui interdit les gros mots et les histoires qui font peur. Une phrase du modèle qui contient un
     gros mot n'est jamais dite, même à un adulte. Une liste de mots ne voit pas tout : un adulte reste à côté de l'enfant.
   - À la main, `parler_au_robot.sh` : la bulle aussi, le journal à l'écran.
   - Le relais tient le port du torse en permanence, sur le Mac ou sur la UNO Q. Il le prête aux annonces de fin de
     tâche, à la programmation (sur le Mac `firmware/flash.sh` et `flash_tete_cou.sh`, par `firmware/avec_port.py` ;
     sur la UNO Q `firmware/flash_uno_q.sh`) et à une conversation à la main, puis le reprend : qui veut le port écrit
     son numéro de processus dans `~/moodimos/port.pause`, et `~/moodimos/port.verrou` (flock) n'en laisse qu'un à la
     fois. Sur le Mac, il reconnaît le torse parmi les cartes USB à sa réponse à `annonce 0`, comme `annonce.py`.
   - La dernière question est gardée dans `derniere_question.wav`, pour entendre ce que le micro a entendu. Elle est
     dans le dossier IA sur le Mac, dans `~/moodimos` sur la UNO Q.
   Le reste de l'architecture (Wi-Fi, musique) est encore à faire.

## Le dossier IA

Tout ce qui est lourd vit hors du dépôt, dans `~/MoodiMOS-IA` par défaut. Pour un autre
emplacement, définir `MOODIMOS_IA`. Une copie du seul dossier MoodiMOS ne suffit donc pas : il faut
aussi ce dossier, ou le refaire comme ci-dessous.

| Chemin dans `MOODIMOS_IA` | Contenu | Utilisé par |
|---|---|---|
| `venv-cerveau/` | Python 3.11 avec `piper-tts` 1.8.0 et `faster-whisper` 1.2.1 | tous |
| `piper/fr_FR-siwis-medium.onnx` et `.onnx.json` | la voix siwis, retenue pour MoodiMOS | tous ceux qui parlent |
| `whisper/` | cache Hugging Face (`HF_HOME`) de `Systran/faster-whisper-small`, lu hors ligne | conversation, jeu à la voix |
| `ollama/` | Ollama 0.40.7 pour macOS (programme `ollama/ollama`) | conversation |
| `ollama-modeles/` | modèles d'Ollama (`OLLAMA_MODELS`) : `ministral-3:3b` | conversation |
| `annonces/` | WAV des annonces gardés en cache, `journal.log` | `annonce.py` |
| `devine_robot/` | les 90 phrases du jeu et `secoue` (humeurs), WAV 16 kHz mono | `vers_robot.py` |

Il faut aussi `ffmpeg` dans le PATH (Homebrew : `brew install ffmpeg`), pour le micro et les effets de voix.

## Refaire l'environnement

```sh
python3.11 -m venv ~/MoodiMOS-IA/venv-cerveau
~/MoodiMOS-IA/venv-cerveau/bin/pip install piper-tts==1.8.0 faster-whisper==1.2.1
```

**Voix siwis :** télécharger `fr_FR-siwis-medium.onnx` et `fr_FR-siwis-medium.onnx.json` (63 Mo)
depuis `https://huggingface.co/rhasspy/piper-voices/resolve/main/fr/fr_FR/siwis/medium/` dans
`~/MoodiMOS-IA/piper/`.

**Whisper :** le premier chargement télécharge le modèle, ensuite les scripts tournent hors ligne
(`HF_HUB_OFFLINE=1`).

```sh
HF_HOME=~/MoodiMOS-IA/whisper ~/MoodiMOS-IA/venv-cerveau/bin/python -c "from faster_whisper import WhisperModel; WhisperModel('Systran/faster-whisper-small', device='cpu', compute_type='int8')"
```

**Ollama** (conversation seulement) : décompresser l'archive macOS d'Ollama dans `~/MoodiMOS-IA/ollama/`,
lancer le serveur avec ses modèles sur le disque, puis, dans un autre terminal, tirer le modèle :

```sh
OLLAMA_MODELS=~/MoodiMOS-IA/ollama-modeles ~/MoodiMOS-IA/ollama/ollama serve
~/MoodiMOS-IA/ollama/ollama pull ministral-3:3b
```

Piper est sous GPL-3.0 : il tourne toujours comme programme séparé, jamais importé dans le code de
MoodiMOS.

## La UNO Q

Sur la UNO Q (`arduino@adresse`, SSH par clé), les programmes vivent dans `~/moodimos` et les modèles sur la
carte SD, montée à la main après chaque démarrage (mot de passe de la UNO Q) :

```sh
ssh -t arduino@adresse udisksctl mount -b /dev/sda1
ssh arduino@adresse 'ln -s /media/arduino/NOM_DE_VOTRE_CARTE/moodimos-ia ~/moodimos/ia'   # une seule fois
```

| Chemin | Contenu |
|---|---|
| `~/moodimos/venv-ia/` | Python 3.13 avec `faster-whisper` 1.2.1, `piper-tts` 1.8.0 et `flask` (serveur de Piper) |
| `~/moodimos/ollama/` | Ollama 0.40.7 pour Linux arm64, sans carte graphique |
| `~/moodimos/ia/piper/` | la voix siwis |
| `~/moodimos/ia/whisper/{tiny,base,small}/` | modèles faster-whisper int8, dossiers simples |
| `~/moodimos/ia/ollama-modeles/` | modèles d'Ollama de la UNO Q (`OLLAMA_MODELS`) : `gemma3:1b` |

Serveur d'Ollama de la UNO Q, une fois après chaque démarrage (cerveau de secours seulement) :

```sh
ssh arduino@adresse 'OLLAMA_MODELS=~/moodimos/ia/ollama-modeles setsid nohup ~/moodimos/ollama/bin/ollama serve > ~/moodimos/ollama.log 2>&1 < /dev/null &'
```

Mesuré le 29/10/2026 sur la UNO Q (4 cœurs Cortex-A53, 3,6 Go) :

- **Whisper :** une question de 2 s est comprise en 3 s (tiny) ou 6 s (base).
- **Voix siwis :** environ 2 fois plus rapide que le temps réel.
- **gemma3:1b :** première phrase en 7 à 14 s, réponses approximatives.
- **Sur le Mac (M1) :** ministral-3:3b donne sa première phrase en moins d'une seconde, avec des réponses justes. Whisper
  small comprend une question de 5 s en 1 s, là où base échoue. Piper fabrique 5 s de voix en 0,2 à 0,3 s.

D'où la conversation sur le Mac, avec la UNO Q en secours. Pour une question au micro du torse, le robot a commencé à
répondre après 12,1 s quand tout tournait sur la UNO Q sauf le cerveau ; le chemin par le Mac vise environ 5 s.

**Le micro du torse entend étouffé.** Il est enfermé derrière l'écran, sans ouverture dans la coque. La voix doit
venir de près (10 à 20 cm) : la crête est alors vers −13 dBFS et la question est comprise. À un mètre, vers −45 dBFS,
les aigus sont noyés dans le bruit de fond et même Whisper small se trompe.

## Lancer

```sh
sh cerveau/service_mac.sh installer      # service de conversation à l'ouverture de la session : la bulle du torse
sh cerveau/service_mac.sh journal        # liaisons, questions et réponses (~/Library/Logs/moodimos-conversation.log)
sh cerveau/service_mac.sh retirer        # plus de service : la bulle reste grise
sh cerveau/parler_au_robot.sh            # parler au robot (USB du Mac ou hub de la UNO Q) ; le Mac écoute, réfléchit, parle
sh cerveau/parler_au_robot.sh uno-q      # même chose sans le Mac : tout sur la UNO Q
sh cerveau/minuteur.sh 20                # minuteur de 20 minutes (ou 1h30, 90 s ; fin : annule ; rien : ce qu'il reste)
~/MoodiMOS-IA/venv-cerveau/bin/python cerveau/essai_conversation.py     # serveur Ollama lancé avant
~/MoodiMOS-IA/venv-cerveau/bin/python cerveau/voix_robot.py "Une phrase à essayer"
~/MoodiMOS-IA/venv-cerveau/bin/python cerveau/annonce.py essai
~/MoodiMOS-IA/venv-cerveau/bin/python extras/devine_animal/devine_animal.py --clavier
```

Au premier démarrage du service, macOS demande que Python puisse lire le disque externe (le dossier IA et le
dépôt) : Autoriser. Sans quoi le service reste bloqué au lancement, journal vide. À refaire si Homebrew change de
version de Python.

Phrases du jeu pour le robot : `table`, puis `voix`, puis `envoyer` (moniteur série du torse fermé, appui long sur l'écran du robot s'il le demande), puis
reprogrammer le torse si la table a changé. Détails en tête de `extras/devine_animal/vers_robot.py`.

Annonces de fin de tâche : un crochet Stop de Claude Code (`~/.claude/settings.json`) et le réglage
`notify` de Codex (`~/.codex/notify_moodimos.sh`) appellent `annonce.py`.

## Qui commande le robot

Le torse n'écoute pas le réseau : on lui parle par son port USB, donc depuis la machine qui le tient (Mac ou UNO Q), y
compris à distance si elle est atteinte. Le firmware ne lui fait donc pas confiance :

| Commandes | Condition |
|---|---|
| `cerveau`, `annonce`, `dis`, `dis+`, `tete`, `capteurs`, `etat`, `minuteur`, `silence`, `acces` | aucune : le service du Mac s'en sert sans personne devant le robot |
| `ecoute` | une seule écoute, dans les 30 s après un échange lancé par un vrai doigt sur la bulle (ouvrir « Qui parle ? » ne suffit pas) ; aucune commande ne simule la bulle, et l'accès ci-dessous n'ouvre pas le micro |
| tout le reste : `camera`, `ecran`, `deposer`, `wifi`, `heure`, `pont`, `jeu`, `dire`, `sons`… | doigt posé 2 s sur l'écran, qui affiche « Autoriser ? » et la commande pendant 20 s ; levé avant, c'est refusé ; accordé pour 10 min. Ni le bisou d'un enfant (1 s) ni des petits coups rapides n'y suffisent ; rien n'est demandé pendant une partie ou une conversation, ni plus d'une fois par minute |

L'état se lit sans passer par un ordinateur. Accès ouvert, le tableau de bord affiche « Accès ouvert : 7 min » à la place
de l'état de la tête ; le voyant CAM montre la caméra, la bulle pleine le micro. À la voix (bulle, puis « quel est l'état
du micro et de la caméra ? », « tu m'écoutes ? ») : réponse du programme, qui lit l'état dans le torse, jamais du modèle.
Dire « le micro » ou « la caméra » suffit ; piratage, enregistrement, « mes données », « mes secrets » aussi. Ces échanges
restent hors de la mémoire du modèle, qui brodait dessus. Une tournure non prévue peut encore partir au modèle : il peut
alors affirmer n'importe quoi sur la sécurité. « Ferme l'accès » le referme tout de suite (`acces fin`).

Le micro tourne en continu dans le torse pour le niveau sonore de l'écran : seul ce chiffre en sort, le son reste dedans.
De même, aucune commande ne fait sortir une image de la caméra (`photo` a été retirée) : seuls la position de ce qui bouge,
pour la tête, et des chiffres en sortent. Le son, le texte et l'image n'ont donc pas à passer par un assistant en ligne
qu'on fait travailler sur le robot : le journal (`service_mac.sh journal`) ne garde les paroles que du propriétaire
(`minuteur` de `personnes.json`) ou d'un robot sans prénoms ni garde-fou ; pour les autres, « Quelqu'un : (12 mots) ».

Ce que cela ne couvre pas : qui tient le port peut encore faire parler le robot (`annonce`), lancer un minuteur, lire les
mesures, et surtout le reprogrammer, car `esptool` passe avant le firmware : un firmware modifié pourrait ouvrir le micro. Verrouiller le téléchargement (eFuses) serait
définitif et empêcherait les mises à jour : non retenu. Le rempart reste donc l'accès au Mac et à la UNO Q. Sur la UNO Q,
une fois la connexion par clé vérifiée, SSH sans mot de passe :

```sh
echo 'PasswordAuthentication no' | sudo tee /etc/ssh/sshd_config.d/10-cle-seulement.conf
sudo systemctl reload ssh
```

## Réglages

| Variable | Par défaut | Rôle |
|---|---|---|
| `MOODIMOS_IA` | `~/MoodiMOS-IA` | dossier IA ci-dessus |
| `MOODIMOS_OLLAMA` | `http://127.0.0.1:11434` | serveur d'Ollama (conversation) |
| `MOODIMOS_MODELE` | `ministral-3:3b` | modèle de la conversation (sur le Mac) |
| `MOODIMOS_MODELE_UNO_Q` | `gemma3:1b` | cerveau de secours de la UNO Q (`conversation_robot.py`) |
| `MOODIMOS_WHISPER` | `Systran/faster-whisper-small` | modèle de reconnaissance sur le Mac ; `base` sur la UNO Q (`tiny`, `base`, `small` de sa carte SD) |
| `UNO_Q` | celle du service, sinon aucune | robot sur le hub de cette UNO Q (`service_mac.sh installer`, `parler_au_robot.sh`) ; vide : sur l'USB du Mac |
| `MOODIMOS_MICRO` | `:0` | micro pour ffmpeg (`ffmpeg -f avfoundation -list_devices true -i ""`) |
| `MOODIMOS_VOIX_ANNONCE` | `robot doux` | voix des annonces, un nom de `voix_robot.py` |
| `MOODIMOS_ANNONCE_MIN` | `30` | durée minimale d'une tâche annoncée, en secondes |
| `MOODIMOS_ANNONCE_ROBOT` | `1` | `0` : annonces par le Mac seulement, même robot branché |
