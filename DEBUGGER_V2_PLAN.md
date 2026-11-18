# Debugger 68000 v2 — plan d'exécution par agents

Ce document est la référence unique pour toutes les tâches. Chaque agent lit d'abord
les sections « Contexte », « Spécification du protocole v2 » et « Règles communes »,
puis **uniquement** sa tâche.

## Contexte

Le debugger live 68000 de BricoNeo se répartit sur deux dépôts git distincts :

| Dépôt | Chemin | Rôle |
|---|---|---|
| **Brico16Bits** (firmware Pico, C++) | `c:\GoogleDrive\Bricolage\Electronique\PicoRoot\Brico16Bits` | Commande `Dbg`, vues classique et TUI, injection du handler dans le BIOS |
| **Brico68k** (ROM de diagnostic, asm vasm) | `C:\GitProjects\Brico68k` | `Trace.s` : handler d'exception TRACE exécuté par le 68000 |

Fonctionnement actuel (v1) :
- **68k → Pico** : un mot est transmis en lisant `BRICO_PORT_OUT` (`$C1FF02`), puis l'adresse
  `$C20000 + valeur*2`. Une lecture de `$C1FF00` (EOT) termine une trame. Côté Pico :
  `InputBuffer::CheckEOTSignalAndClearEOT()` puis `InputBuffer::GetValue()`.
- **Pico → 68k** : le Pico écrit dans sa propre image ROM système (`ROMImageManager::GetImageBaseAddressB()`,
  offsets `$1FFxx`), que le 68k relit en `$C1FFxx`.
- À chaque pas, le handler envoie une trame de 59 mots (registres, SR, PC, 25 mots de code,
  compteur), puis attend que le compteur `$C1FFFE` change, et lit la commande en `$C1FFFC`.
- Pour un BIOS standard, `CommandDebug` injecte le handler (`gwTraceRoutine[87]`, copie
  manuelle du code assemblé de `Trace.s`) à l'offset `$4200` et patche `$0420` avec un `jmp`.
- Le code débuggé peut être en ROM système (BIOS, Brico68k) **ou en cartouche** (P-ROM, que
  le Pico ne voit pas : tout accès passe par le 68k).

**Deux modes d'utilisation du même `Trace.s` — les deux doivent fonctionner :**

| Mode | Comment le handler arrive en mémoire | Ce que fait `Dbg` |
|---|---|---|
| **A. BIOS standard** (+ jeu cartouche) | Code de `Trace.s` assemblé seul (T3), injecté à `$C04200`, puis appelé via le `jmp` patché en `$0420` | Patche le BIOS, puis reset |
| **B. ROM Brico68k** chargée comme ROM système | `Trace.s` est assemblé et lié dans la ROM Brico68k ; le vecteur TRACE (`dc.l TRACE` dans `Brico68k.s`) pointe dessus | Ne patche rien (« non conventional system ROM »), reset seulement |

Conséquences :
- `Trace.s` doit s'assembler **à l'identique** dans les deux cas : aucune référence à un symbole
  externe de Brico68k, seulement des `include` de fichiers `inc/` qui ne définissent pas de labels.
- En mode B, le code tracé (Brico68k) utilise lui-même `BRICO_PORT_OUT` / EOT pour son protocole
  BricoExt. En pas à pas ou en RUN_UNTIL dans ce code, des mots parasites et de faux EOT peuvent
  donc arriver côté Pico **entre deux arrêts**. Le Pico doit y résister (voir « Resynchronisation »).
  Pendant que le 68k est dans le handler, le code tracé ne tourne pas : les réponses aux commandes
  ne peuvent pas être polluées.

Objectifs v2 :
1. Une trame minimale à chaque arrêt, puis une boucle de commandes où le Pico demande ce
   dont il a besoin (registres, mémoire).
2. Une fenêtre TUI de dump mémoire, rafraîchie à chaque arrêt, avec les octets modifiés surlignés.
3. Step over / step out / break, grâce à une comparaison du PC faite côté 68k (RUN_UNTIL).
4. Pile d'appels (adresse de l'appelant).

**Pas de compatibilité avec la v1** : l'ancien format de trame est supprimé des deux côtés.

## Spécification du protocole v2

Les constantes sont définies dans `Brico16Bits/Commands/DebugProtocol.h` (C++) et dans
`Brico68k/inc/trace.inc` (asm). **Les valeurs doivent être identiques dans les deux fichiers.**

### Patch du BIOS

| Constante | Valeur | Note |
|---|---|---|
| `BIOS_PATCH_TRACE_ROUTINE_OFFSET` | `0x4200` | Offset dans l'image ROM système → `$C04200` |
| `BIOS_PATCH_TRACE_ROUTINE_MAX_BYTES` | `0x200` | Valeur fixée par l'utilisateur pour le moment (mode A uniquement) |

### Boîte aux lettres Pico → 68k

Offsets dans l'image ROM système ; le 68k lit à `$C00000 + offset`. `$1FFFA` est **réservé**
(BricoExt / CommandManager), il ne faut pas l'utiliser.

| Constante | Offset | Taille | Rôle |
|---|---|---|---|
| `DEBUG_MAILBOX_ARG0` | `0x1FFF0` | long | Adresse (READ_MEM) / PC cible (RUN_UNTIL) |
| `DEBUG_MAILBOX_ARG1` | `0x1FFF4` | long | Nombre de mots (READ_MEM) / SP minimal (RUN_UNTIL) |
| `DEBUG_MAILBOX_BREAK` | `0x1FFF8` | word | ≠ 0 : arrêter au prochain passage dans le handler |
| `DEBUG_MAILBOX_CMD` | `0x1FFFC` | word | Code de commande |
| `DEBUG_MAILBOX_CTR` | `0x1FFFE` | word | Compteur de requête, **écrit en dernier** par le Pico |

Les mots et les longs sont stockés en big-endian du point de vue du 68k. Le code actuel
écrit chaque octet séparément ; il faut conserver la même convention d'octets que
`CommandDebug.cpp` aujourd'hui (octet de poids faible à l'offset pair).

### Commandes

| Code | Nom | Arguments | Effet côté 68k |
|---|---|---|---|
| 0 | `STEP` | — | Restaure l'état, `rte` (T reste à 1) |
| 1 | `CONTINUE` | — | Met T à 0 dans le SR empilé, `rte` |
| 2 | `GET_REGS` | — | Envoie la réponse registres + EOT, reste dans la boucle |
| 3 | `READ_MEM` | ARG0 = adresse (paire), ARG1 = n mots (1..256) | Envoie n mots + EOT, reste dans la boucle |
| 4 | `RUN_UNTIL` | ARG0 = PC cible, ARG1 = SP minimal | `rte` avec T à 1 ; les passages suivants prennent la voie rapide |

`DEBUG_PROTOCOL_MAGIC = 0xDB02`.

### Trame d'arrêt (68k → Pico), 5 mots puis EOT

`MAGIC`, `SR`, `PC hi`, `PC lo`, `CTR` (valeur lue dans `DEBUG_MAILBOX_CTR` à l'entrée).

### Réponse GET_REGS, 37 mots puis EOT

`d0..d7` (2 mots chacun, poids fort d'abord), `a0..a6` (2 mots chacun), `SSP_INTERRUPTED`
(2 mots : valeur de SSP **avant** l'exception, c'est-à-dire adresse du cadre d'exception + 6),
`USP` (2 mots), `SR`, `PC hi`, `PC lo`.
Le Pico affiche comme a7 : `SR.S ? SSP_INTERRUPTED : USP`.

### Poignée de main

1. Le 68k envoie la trame d'arrêt + EOT et mémorise `ctr` (valeur envoyée).
2. Le Pico lit la trame. Pour chaque requête : il écrit ARG0/ARG1, `BREAK = 0` et CMD, puis
   `CTR = ctr + 1`, et attend EOT. Il lit exactement le nombre de mots attendu, puis considère
   `ctr = ctr + 1`.
3. Le 68k boucle (en servant le watchdog) tant que `CTR == ctr`. Il lit ensuite CMD, exécute,
   met `ctr = CTR`, et reboucle ou sort selon la commande.
4. Au lancement de `Dbg`, le Pico met toute la boîte aux lettres à 0 avant le reset du 68k.

### Resynchronisation côté Pico

Une trame d'arrêt valide est formée des **5 derniers mots reçus avant un EOT**, dont le premier
vaut `MAGIC`. Quand `WaitStop` reçoit un EOT, il vide tous les mots en attente et ne garde que les
5 derniers :
- moins de 5 mots, ou pas de `MAGIC` en tête : EOT émis par le code tracé, on l'**ignore
  silencieusement** et on continue d'attendre ;
- `MAGIC` présent : arrêt valide.

Il n'y a jamais d'erreur bloquante pendant cette attente. Si un EOT arrive avec **5 mots ou plus**
mais sans `MAGIC`, `WaitStop` incrémente un compteur de trames non reconnues. L'interface affiche
alors un message non bloquant (« trame non reconnue reçue — handler TRACE d'une autre version ? »)
et continue d'attendre. C'est ce qui arrive avec un handler v1 (59 mots). Les réponses
aux commandes (GET_REGS, READ_MEM) ne sont pas concernées : leur longueur est connue et elles ne
peuvent pas être polluées.

### Voie rapide RUN_UNTIL, au tout début du handler

Elle s'exécute après le service du watchdog et avant toute sauvegarde de registres. Si
`CMD == RUN_UNTIL`, `BREAK == 0` et que la condition « `PC empilé == ARG0` et
`SSP_INTERRUPTED >= ARG1` » est **fausse** : on restaure le minimum et on fait `rte`
(T reste à 1). Sinon, on prend le chemin complet (trame d'arrêt + boucle de commandes).
La voie rapide ne doit modifier aucun registre ni aucun flag du code tracé.

### Contraintes du handler 68k

- **Position-indépendant** : relogé à `$C04200` en mode A, à son adresse de lien en mode B.
  Uniquement des `bsr`/`bra` relatifs ; les adresses absolues ne sont permises que pour les ports
  `$C1FFxx`, `$C2xxxx` et `$300001`.
- **Autonome** : aucune référence à un label défini ailleurs dans Brico68k. Les constantes du
  protocole ont des noms préfixés `TRACE_` / `DEBUG_`, pour éviter les collisions avec
  `inc/define.inc` (qui définit déjà `BRICO_PORT_OUT`...).
- Aucune utilisation de RAM en dehors de la pile.
- Watchdog (`move.b d0,$300001`) servi dans toutes les boucles d'attente et dans la voie rapide.
- Taille ≤ `BIOS_PATCH_TRACE_ROUTINE_MAX_BYTES`.
- Tous les registres du code tracé sont restaurés à l'identique.

## Règles communes à tous les agents

- **Aucun commit, aucun push.** Travailler dans l'arbre de travail ; l'orchestrateur commitera
  après validation de l'utilisateur.
- **Compilation obligatoire** avant de rendre la tâche :
  - Brico68k : `cmake --build C:/GitProjects/Brico68k/build`
  - Brico16Bits : `cmake --build C:/GoogleDrive/Bricolage/Electronique/PicoRoot/build --target Brico16Bits`
- Respecter le style existant : préfixes de nommage (`mn`, `mp`, `mb`, `gw`...), commentaires
  `/// @brief` en anglais, classes statiques `XxxManager`.
- Ne modifier que les fichiers listés dans la tâche. Si un autre fichier doit changer, le
  signaler dans le compte rendu plutôt que de le modifier.
- **Aucun agent ne peut tester sur le matériel.** Le compte rendu doit lister précisément ce
  que l'utilisateur doit vérifier sur la Neo Geo.
- Compte rendu final : fichiers modifiés, résultat de la compilation, écarts par rapport à la
  spécification, points de test matériel.

## Tâches

Légende des modèles : **Opus** = raisonnement délicat (asm 68k, protocole, heuristiques) ;
**Sonnet** = implémentation C++ / TUI bien spécifiée ; **Haiku** = mécanique ou documentation.

### Phase 1 — Protocole v2 (sans nouvelle fonction visible)

#### T1 — Constantes partagées · Sonnet · dépendances : aucune
- Créer `Brico16Bits/Commands/DebugProtocol.h` avec toutes les constantes de la spécification
  (patch BIOS, boîte aux lettres, magic, `enum class DebugCommand : uint16_t`, tailles de trame
  `DEBUG_STOP_FRAME_WORDS = 5` et `DEBUG_REGS_RESPONSE_WORDS = 37`,
  `DEBUG_READ_MEM_MAX_WORDS = 256`).
- Ajouter les mêmes valeurs en `equ` dans `Brico68k/inc/trace.inc`, avec les adresses 68k
  (`$C00000 + offset`). Retirer les `equ` correspondants de `Trace.s` s'ils y sont dupliqués.
- Ajouter `DebugProtocol.h` dans `Brico16Bits/CMakeLists.txt` (liste HEADERS).
- `Brico68k/CMakeLists.txt` : les objets ne dépendent aujourd'hui que de `inc/define.inc`
  (`INC_FILE`). Faire dépendre chaque `.o` de **tous** les `inc/*.inc` (au minimum `trace.inc`),
  pour qu'une modification du protocole recompile bien la ROM.
- Critère : les deux projets compilent, et un tableau de correspondance (C++ ↔ asm) figure
  dans le compte rendu.

#### T2 — Handler `Trace.s` v2 · **Opus** · dépendances : T1
- Réécrire `Brico68k/Trace.s` selon la spécification : voie rapide RUN_UNTIL, trame d'arrêt,
  boucle de commandes, `GET_REGS`, `READ_MEM`, `STEP`, `CONTINUE`, `RUN_UNTIL`. Les commandes
  inconnues sont traitées comme `STEP`.
- Conserver `global TRACE` et les macros de `inc/trace.inc` (`TraceOn`, `Breakpoint`, `TraceOff`).
- Fichiers : `Trace.s`, éventuellement `inc/trace.inc` (macros uniquement).
- Le handler doit fonctionner dans les modes A **et** B (voir « Contexte »).
- Critères : la ROM Brico68k compile (mode B) et `Trace.s` s'assemble seul sans symbole non
  résolu (mode A, cible `trace_routine` de T3 si elle existe déjà, sinon `vasm -Fbin` à la main) ;
  la taille du handler en octets (voir `build/precomp/Trace.s.txt`) est indiquée et comparée à
  `BIOS_PATCH_TRACE_ROUTINE_MAX_BYTES` (0x200) ;
  pas d'adressage absolu hors des ports autorisés ; analyse écrite, pour chaque chemin, de la
  restauration des registres et des flags.

#### T3 — Génération automatique de `TraceRoutine.h` · Sonnet · dépendances : aucune (à relancer après T2)
- Dans `Brico68k/CMakeLists.txt`, ajouter une cible `trace_routine` (dans `ALL`). Elle assemble
  `Trace.s` seul en binaire brut (`vasm -Fbin`, mêmes `ASFLAGS` sauf le format de sortie), puis
  un script `cmake/BinToHeader.cmake` produit `build/TraceRoutine.h`. Ce fichier contient
  `static const uint16_t gwTraceRoutine[] = {...};` (mots big-endian, comme le tableau actuel)
  et `#define TRACE_ROUTINE_SIZE_BYTES n`.
- Aucune dépendance entre les deux projets : Brico68k produit seulement `build/TraceRoutine.h`.
  On le copie ensuite à la main dans `Brico16Bits/Commands/TraceRoutine.h`.
- **Validation de non-régression** : exécutée sur le `Trace.s` **actuel** (avant T2), la cible
  doit produire exactement les 87 mots de `CommandDebug::gwTraceRoutine`. Mettre le diff dans le
  compte rendu.
- Fichiers : `Brico68k/CMakeLists.txt`, `Brico68k/cmake/BinToHeader.cmake`.

#### T4 — Classe `DebugLink` (Pico) · Sonnet · dépendances : T1
- Créer `Brico16Bits/Commands/DebugLink.h/.cpp`, qui encapsule le protocole :
  - `static void ResetMailbox();`
  - `bool WaitStop(DebugStop *stop, PollCallback poll)` : attend une trame d'arrêt. `poll` est
    appelé pendant l'attente ; s'il renvoie `false`, l'attente est abandonnée. La fonction
    applique la règle de la section « Resynchronisation » : les EOT parasites émis par le code
    Brico68k tracé (mode B) sont ignorés sans erreur.
  - `bool GetRegisters(DebugRegisters *regs)`, `bool ReadMemory(uint32_t address, uint16_t words, uint16_t *buffer)`.
    `ReadMemory` refuse une adresse impaire, `words > 256`, et la zone `$300000-$3FFFFF` sauf si
    `force` est vrai.
  - `void Resume(DebugCommand cmd, uint32_t arg0 = 0, uint32_t arg1 = 0)` pour STEP, CONTINUE et RUN_UNTIL.
  - `void RequestBreak()` : écrit `BREAK = 1`.
  - Timeout configurable sur les réponses ; en cas d'expiration, la fonction renvoie `false` avec
    un code d'erreur.
- Pas d'interface utilisateur dans cette classe.
- Ajouter les fichiers dans `CMakeLists.txt`.
- Critère : compile ; le compte rendu détaille l'ordre des écritures dans la boîte aux lettres et
  la gestion d'EOT.

#### T5 — Intégration dans `CommandDebug` · Sonnet · dépendances : T3, T4 (et T2 pour le header final)
- `CommandDebug.cpp/.h` : utiliser `DebugLink` à la place de la lecture brute de trame et des
  écritures `0x1fffc`/`0x1fffe`. Supprimer `FRAME_SIZE`, l'ancien `DecodeFrame` et le tableau
  `gwTraceRoutine` (remplacé par `#include "TraceRoutine.h"`).
- Patch du BIOS : le `jmp` en `$0420` vise `$C00000 + BIOS_PATCH_TRACE_ROUTINE_OFFSET`.
  Ajouter `static_assert(TRACE_ROUTINE_SIZE_BYTES <= BIOS_PATCH_TRACE_ROUTINE_MAX_BYTES)`.
  Appeler `DebugLink::ResetMailbox()` avant le reset.
- À chaque arrêt : `GetRegisters()` + `ReadMemory(PC, DEBUG_FRAME_CODE_WORDS)` remplissent
  `DebugFrame` (ajouter les champs `ssp` et `usp`). a7 affiché = `SR.S ? SSP : USP`.
- Magic incorrect : message d'erreur explicite dans les deux vues, puis sortie propre.
- Fichiers : `CommandDebug.*`, `DebugFrame.*`, `DebugTuiView.cpp` (affichage de a7 uniquement).
- Critère : compile ; les vues affichent la même chose qu'en v1, a7 corrigé.

#### T6 — Revue croisée du protocole · **Opus** · lecture seule · dépendances : T2, T5
- Vérifier que `Trace.s` et `DebugLink`/`CommandDebug` respectent la spécification de bout en
  bout, **dans les modes A et B** : offsets, ordre des octets, nombre de mots, poignée de main, EOT, situations de
  concurrence (BREAK pendant RUN_UNTIL, requête envoyée avant la fin de lecture d'une réponse,
  mots et EOT parasites émis par le code BricoExt tracé en mode B),
  conservation des registres, position-indépendance.
- Rendu : liste de défauts classés par gravité, avec fichier:ligne. Aucune modification.

#### G1 — Validation matérielle (utilisateur)
Le comportement doit être identique à la v1, dans les deux modes :
- **Mode A** : BIOS standard + jeu cartouche (pas à pas, continue).
- **Mode B** : ROM Brico68k (`Breakpoint` dans le code, pas à pas, continue), y compris un pas à
  pas **à travers** une routine qui envoie des données BricoExt (ex. `port_write`, `WriteEOT`) :
  aucune fausse trame ni erreur côté Pico.

### Phase 2 — Fenêtre mémoire

#### T7a — Logique pure du dump · Sonnet · dépendances : T5 · parallélisable avec T9
- Nouveau `Brico16Bits/Commands/DebugExpression.h/.cpp` : parse `"c0a000"`, `"$c0a000"`,
  `"0x..."`, `"d0".."d7"`, `"a0".."a7"`, `"sp"`, `"pc"`, avec un `+`/`-` hexadécimal optionnel
  (`a6-$10`). Les valeurs de registres viennent d'un `DebugFrame`.
- Nouveau `Brico16Bits/Commands/MemoryDump.h/.cpp` : stocke la zone courante (adresse, octets)
  et la précédente ; `IsByteChanged(address)` compare uniquement les octets présents dans les
  deux dumps.
- Tests hôte : `Brico16Bits/HostTests/` (un `main` compilé avec le g++ de MSYS2,
  `C:/msys64/mingw64/bin/g++`), qui couvre le parseur et la comparaison. Non intégré au build Pico.

#### T7b — Panneau mémoire TUI + invite · Sonnet · dépendances : T7a
- `DebugTuiView` : panneau hexa + ASCII, 16 octets par ligne, adresses sur 6 chiffres. Octets
  modifiés en `COLOR_VALUE_CHANGED` + gras, sur l'hexa **et** sur l'ASCII. En dessous de 32
  lignes, F3 bascule entre le code et la mémoire ; à partir de 32 lignes, les deux panneaux
  s'affichent ensemble (`ComputeLayout()`).
- Invite de saisie dans la ligne de message (Tui n'a pas d'édition de ligne : ajouter une
  fonction réutilisable `Tui::ReadLine(row, col, width, buffer, size)` qui gère Backspace,
  Entrée et Échap).
- `CommandDebug` : touche `m` pour saisir l'adresse, PgUp/PgDn pour défiler. À chaque arrêt,
  `ReadMemory` de la zone affichée. En vue classique, `m` demande l'adresse au prompt et
  affiche un dump sur 8 lignes.
- Mettre à jour la barre d'aide et le texte d'aide de la commande.
- Fichiers : `DebugTuiView.*`, `CommandDebug.*`, `Tui.*`.

#### G2 — Validation matérielle
Dump RAM `$100000` pendant le pas à pas : les octets modifiés passent en jaune ; défilement ;
bascule F3 ; dump d'une adresse cartouche.

### Phase 3 — Step over / step out / break

#### T8 — Commandes RUN_UNTIL côté Pico · Sonnet · dépendances : G1, T7b (fichiers communs)
- Ajouter `DebugFrame.h` : `InstructionFlow ClassifyInstruction(const uint16_t *words)`, qui
  renvoie Call (`JSR`, `BSR`), Trap (`TRAP`, `TRAPV`), Loop (`DBcc`), Return (`RTS`, `RTR`, `RTE`)
  ou Other. L'enum existe déjà dans `Commands/InstructionFlow.h` (créé par T9) : l'inclure, ne
  pas la redéfinir. Ajouter des tests dans `HostTests/` (fichier et script propres à T8, sur le
  modèle de `TestCallStack.cpp` / `build_callstack.sh`).
- `F10` / `o` (step over) : pour Call, Trap ou Loop, `Resume(RUN_UNTIL, PC + taille, SP courant)` ;
  sinon STEP.
- `b` pendant RUN_UNTIL : `RequestBreak()`. État « Running (until $xxxxxx) » dans la barre de titre.
- `g` (run to) : demande une adresse (expression) puis `RUN_UNTIL(addr, 0)`.
- Fichiers : `CommandDebug.*`, `DebugTuiView.*`, `DebugFrame.*`, `HostTests/`.

#### G3 — Validation matérielle
Step over d'un `jsr` dans Brico68k et dans un jeu ; break sur une boucle d'attente de VBlank ;
`g` vers une adresse ; fonction récursive si possible.

### Phase 4 — Pile d'appels

#### T9 — Analyseur de pile d'appels · **Opus** · dépendances : T5 · parallélisable avec T7a
- Nouveau `Brico16Bits/Commands/CallStack.h/.cpp`, logique pure, avec un callback de lecture
  mémoire (testable sur hôte) :
  1. **Pile d'appels tenue par le Pico** : `OnStep(prevPc, instructionFlow, size, newPc, newSp)` ;
     Call → empile l'adresse de retour (et le SP) ; Return → dépile jusqu'au SP correspondant.
     Elle est invalidée après CONTINUE.
  2. **Chaîne A6** : `(A6)` contient l'ancien A6 et `4(A6)` l'adresse de retour. Vérifications :
     adresse en RAM `$100000-$10FFFF`, paire, strictement croissante, au plus 16 niveaux.
  3. **Heuristique sur la pile** : parcourt jusqu'à 128 mots à partir de SP ; un long est retenu
     s'il est pair, dans une zone de code (`$000000-$0FFFFF`, `$200000-$2FFFFF`,
     `$C00000-$C1FFFF`), et précédé d'un `JSR`/`BSR` valide (opcode à `-2`, `-4` ou `-6` selon le
     mode d'adressage).
  - Résultat : liste d'appelants, chacun avec la méthode qui l'a trouvé. Priorité : 1 si elle est
    valide, sinon 2, sinon 3.
- Tests hôte avec des images mémoire synthétiques, pour chaque méthode.
- **Réalisé — écarts par rapport à ce qui précède** (l'API réelle est dans `CallStack.h`) :
  - `InstructionFlow` est défini dans `Commands/InstructionFlow.h` (sans dépendance).
  - `OnStep(prevPc, prevSp, flow, size, newPc, newSp)` : paramètre `prevSp` en plus, pour vérifier
    l'effet sur SP (Call : −4, Trap : −6 ; tout autre effet invalide la pile fantôme).
  - `OnRunUntilReached(newSp)` après un step over / step out arrêté sur sa cible.
  - `Caller { returnAddress, frameSp, method }` : `frameSp` est l'adresse du mot de pile qui
    contient l'adresse de retour.
  - Les méthodes s'enchaînent cadre par cadre (fantôme, puis A6 au-dessus, puis scan au-dessus) au
    lieu de « 1, sinon 2, sinon 3 » ; chaque appelant porte sa méthode.
  - Zone de code P-ROM à partir de `$000100` (table des vecteurs exclue) ; adresses de retour A6
    également soumises au test JSR/BSR ; cadres A6 croissant d'au moins 8 octets.
  - Limite : une seule pile (superviseur). Un TRAP pris en mode utilisateur invalide la pile
    fantôme. Sans conséquence pratique : les TRAP ne sont pas utilisés sur Neo Geo (ne pas
    chercher à tester ce cas en G4).

#### T10 — Panneau « Callers » + step out · Sonnet · dépendances : T9, T8
- Panneau TUI (3 à 5 lignes : adresse + méthode), intégré à `ComputeLayout()`. En vue classique,
  une ligne `Caller: $xxxxxx (shadow|a6|scan)`.
- `F11` / `u` (step out) : `RUN_UNTIL(callers[0].returnAddress, callers[0].frameSp + 4)`.
- Tenue de la pile fantôme (`CallStack`, instance unique dans `CommandDebug`) :
  - `Reset()` au lancement de `Dbg` et à chaque reset du 68k.
  - Après un STEP : `OnStep(prevPc, prevSp, ClassifyInstruction(...), taille, newPc, newSp)`, avec
    l'instruction et les registres de l'arrêt précédent.
  - Après un step over / step out arrêté sur sa cible (SP ≥ SP minimal) : `OnRunUntilReached(newSp)`.
  - `Invalidate()` après : CONTINUE (et le break qui suit), `g`, tout RUN_UNTIL arrêté ailleurs que
    sur sa cible (break `b`), toute erreur de `DebugLink` (timeout, `WaitStop` abandonné,
    `GET_REGS` en échec).
- `Analyze(sp, a6, memory, callers, maxCallers)` avec `maxCallers` = taille du panneau (3 à 5) :
  l'analyse s'arrête dès qu'elle est atteinte, ce qui borne les lectures. Toutes les lectures
  passent par `DebugLink::ReadMemory` (pas de lecture directe dans l'image ROM du Pico).
  Shift n'est pas décodé par `Tui`, d'où F11 et non Shift+F11.
- Les données nécessaires aux méthodes 2 et 3 sont lues avec `ReadMemory` à chaque arrêt
  (128 mots de pile au maximum).
- Fichiers : `CommandDebug.*`, `DebugTuiView.*`.

#### G4 — Validation matérielle
Callers corrects dans Brico68k (asm, sans `LINK`) et dans un jeu ; step out.

### Phase 5 — Documentation

#### T11 — Documentation · Haiku · dépendances : T10
- `Brico16Bits/README.md` : section Dbg (nouvelles touches, fenêtre mémoire, step over/out,
  callers).
- `Brico68k/HARDWARE_NOTES.md` : section « Protocole debugger v2 », qui reprend la spécification
  ci-dessus.
- Supprimer ce fichier de plan, ou le déplacer dans `Docs/`, selon la décision de l'utilisateur.

## Ordonnancement

```
Vague 1 : T1, T3 (validation sur l'ancien Trace.s)        — en parallèle
Vague 2 : T2, T4                                           — en parallèle
Vague 3 : T3 relancé (header v2) → T5
Vague 4 : T6 (revue) → corrections → G1
Vague 5 : T7a ∥ T9  →  T7b  → G2
Vague 6 : T8 → G3
Vague 7 : T10 → G4
Vague 8 : T11
```

Fichiers partagés, à ne jamais modifier en parallèle : `CommandDebug.*` (T5, T7b, T8, T10),
`DebugTuiView.*` (T5, T7b, T8, T10), `DebugFrame.*` (T5, T8), `CMakeLists.txt` de Brico16Bits
(T1, T4, T7a, T9 : ajout de fichiers ; regroupé par l'orchestrateur en cas de conflit).

## Points ouverts (utilisateur)

- `BIOS_PATCH_TRACE_ROUTINE_MAX_BYTES` est fixé à `0x200` pour le moment. Si le handler v2 dépasse
  cette taille, le `static_assert` de T5 échoue, et l'utilisateur doit indiquer un autre
  emplacement ou une autre taille. La limite ne concerne que le mode A.
- Commits : un commit par phase validée (G1..G4) dans chacun des deux dépôts, une fois que
  l'utilisateur a donné son accord.

## Défauts connus hors plan (ne pas corriger sans accord de l'utilisateur)

### Désassembleur : opcodes manquants dans `OpcodeDecoder.cpp`
Constaté pendant G1 (mode B, arrêt sur `WAIT_FOR_COMMAND` après un `Breakpoint`). Défaut
antérieur à la v2 (`OpcodeDecoder.cpp` inchangé), **pas un échec de G1** : le PC et les mots de
code transmis sont corrects.

- Symptôme : `46FC 2700` (`move #$2700,sr`, macro `DisableInterrupts`) s'affiche `Unknown`. Le
  décodeur n'avance alors que d'un mot et décode la valeur immédiate `2700` comme une
  instruction (`move.l d0,-(a3)`). Les instructions suivantes se réalignent ensuite.
- Absents de la table `OpcodeDecoder` (fin de `OpcodeDecoder.cpp`) :
  - `MOVE to SR` : masque `$FFC0`, valeur `$46C0` (alors que `MoveFromSR` et `MoveToCCR` existent) ;
  - `ORI to SR` `$007C`, `ANDI to SR` `$027C`, `EORI to SR` `$0A7C` (seules les variantes CCR
    existent ; ces opcodes tombent dans le motif générique `OrI`/`AndI`/`EorI` avec un mode
    d'adressage immédiat invalide). Cela touche les macros `TraceOn`/`TraceOff`
    (`ori.w #$8000,sr`, `and.w #$7fff,sr`) ;
  - `MOVE USP` (`$4E60`/`$4E68`), `STOP` (`$4E72`), `RESET` (`$4E70`), `TRAPV` (`$4E76`).
- Conséquence pour T8 : la taille d'instruction utilisée pour le step over vient du
  désassemblage. Les instructions Call/Trap/Loop concernées (`JSR`, `BSR`, `TRAP`, `DBcc`) sont
  décodées ; `TRAPV` ne l'est pas, mais il fait un mot, ce qui correspond à l'avance d'un
  `Unknown`.
