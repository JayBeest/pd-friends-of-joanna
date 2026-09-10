# Character bios

Source of truth for every `struct chrbio` entry the port compiles.
Edit here, then run `tools/mkchrbios build` to write it into
`src/game/training.c`. Never edit the generated region in the C by hand.

Two kinds of entry:

- **literal** — the text is authored in this document and compiled in as a
  C string literal.
- **lang** — the text lives in the ROM's own language bank and is *not*
  copied here; only the four `L_*` symbol names are recorded, so the game
  keeps showing it in the player's language.

Inside a bio, `###` becomes a `|Heading -` section and everything else is
emitted verbatim — **a line break in this document is a line break in the
game**, so keep a paragraph on one line unless you mean to wrap it.

---

## Joanna Dark

- **index** `0`
- **kind** `literal`
- **file** `src/game/training.c`
- **symbol** `bios[0]`
- **flags** `CHRBIO_FLAG_LITERAL`
- **race** `cI-027 Replicant (Female)`
- **age** `23 (apparent)`

### CI File #027

Training Status: Complete
Training Grade: A++
Active Status: Assigned
Manufacturer: Carrington

### Profile

Highly trained but inexperienced. Reactions superb. Proficient with a variety of weapons. Very competent all-round agent. Highest recorded training scores resulted in the creation of a new class of training grade. The embodiment of the Carrington Institute's ideal agent, hence the call sign 'Perfect Dark.'

---

## Velvet Dark

- **index** `1`
- **kind** `literal`
- **file** `src/game/training.c`
- **symbol** `bios[1]`
- **flags** `CHRBIO_FLAG_LITERAL`
- **race** `cI-042 Replicant (Female)`
- **age** `23 (apparent)`

### CI File #042

Training Status: Complete
Training Grade: A++
Active Status: Assigned
Manufacturer: Carrington

### Profile

Sister combat replicant to agent Perfect Dark. Experiences are transferred between locally active friend agents. Variation between agents may reduce psychological intrusions regarding their nature, but cause occasional glitches during memory transfer. More research must be conducted with experimental control agent 'Jonathan.' Couches at the institute are made with the highest quality velvet, hence the call sign 'Velvet Dark.'

---

## Mikado Dark

- **index** `2`
- **kind** `literal`
- **file** `src/game/training.c`
- **symbol** `bios[2]`
- **flags** `CHRBIO_FLAG_LITERAL`
- **race** `cI-101 Replicant (Female)`
- **age** `23 (apparent)`

### CI File #101

Training Status: Complete
Training Grade: A++
Active Status: Assigned
Manufacturer: Carrington

### Profile

Tokyo's instantiation of the Perfect Dark combat agent but with localized experiential modules. Their likeness and personality is influenced by an amalgamation of Japanese cultural media training data collected from the years 1980-1990. Some of the agent's training data has been back ported to the other Perfect Dark agents to aid in better cooperation between friends. The perfect embodiment of Japanese corporate values, hence the call sign 'Mikado Dark.'

---

## Poplin Dark

- **index** `3`
- **kind** `literal`
- **file** `src/game/training.c`
- **symbol** `bios[3]`
- **flags** `CHRBIO_FLAG_LITERAL`
- **race** `dD-100 Replicant (Female)`
- **age** `20 (apparent)`

### CI File #081

Training Status: N/A
Training Grade: N/A
Active Status: Unassigned
Manufacturer: dataDyne

### Profile

First-generation dataDyne-produced autonomous combat agent. Pre-loaded with extracted Joanna Dark personality and skill models. Mission to destroy the [redacted] in Africa was successful, but the agent was retired after found to have suffered from data contamination. Given this agent's inextricable moral compass, they can still can be repurposed for emergencies if their friends' lives are at risk, hence the call sign 'Poplin Dark.'

---

## Calico Dark

- **index** `4`
- **kind** `literal`
- **file** `src/game/training.c`
- **symbol** `bios[4]`
- **flags** `CHRBIO_FLAG_LITERAL`
- **race** `dD-200x Replicant (Female)`
- **age** `27 (apparent)`

### CI File #141

Training Status: N/A
Training Grade: N/A
Active Status: Unassigned
Manufacturer: dataDyne

### Profile

Experimental dataDyne protoype unit. A composite of experiential modules harvested from anonymous engramboards. It was fitted with self-replicating neurotic modules intended to render it docile and subservient. dataDyne records indicate this unit killed its handlers during a botched affinity calibration sequence. Caution is advised when interacting with this agent.

---

## Willow Dark

- **index** `5`
- **kind** `literal`
- **file** `src/game/training.c`
- **symbol** `bios[5]`
- **flags** `CHRBIO_FLAG_LITERAL`
- **race** `Lorem ipsum dolor (sit amet)`
- **age** `00 (consectetur)`

### Lorem Ipsum

Dolor Sit: Amet
Consectetur: Adipiscing
Sed Eiusmod: Tempor
Incididunt: Ut Labore

### Dolore Magna

Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.

---

## Jonathan

- **index** `6`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[6]`
- **flags** `0`
- **strings** `L_MISC_223, L_MISC_224, L_MISC_225, L_MISC_226`

_Text lives in the language bank; not duplicated here._

---

## Daniel Carrington

- **index** `7`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[7]`
- **flags** `0`
- **strings** `L_MISC_227, L_MISC_228, L_MISC_229, L_MISC_230`

_Text lives in the language bank; not duplicated here._

---

## Cassandra De Vries

- **index** `8`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[8]`
- **flags** `0`
- **strings** `L_MISC_231, L_MISC_232, L_MISC_233, L_MISC_234`

_Text lives in the language bank; not duplicated here._

---

## Trent Easton

- **index** `9`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[9]`
- **flags** `0`
- **strings** `L_MISC_235, L_MISC_236, L_MISC_237, L_MISC_238`

_Text lives in the language bank; not duplicated here._

---

## Dr. Caroll

- **index** `10`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[10]`
- **flags** `0`
- **strings** `L_MISC_239, L_MISC_240, L_MISC_241, L_MISC_242`

_Text lives in the language bank; not duplicated here._

---

## Elvis

- **index** `11`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[11]`
- **flags** `0`
- **strings** `L_MISC_243, L_MISC_244, L_MISC_245, L_MISC_246`

_Text lives in the language bank; not duplicated here._

---

## Mr. Blonde

- **index** `12`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[12]`
- **flags** `0`
- **strings** `L_MISC_247, L_MISC_248, L_MISC_249, L_MISC_250`

_Text lives in the language bank; not duplicated here._

---

## Mr. Blonde (repeat)

- **index** `13`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[13]`
- **flags** `0`
- **strings** `L_MISC_251, L_MISC_252, L_MISC_253, L_MISC_254`

_Text lives in the language bank; not duplicated here._

---

## The U.S. President

- **index** `14`
- **kind** `lang`
- **file** `src/game/training.c`
- **symbol** `bios[14]`
- **flags** `0`
- **strings** `L_MISC_255, L_MISC_256, L_MISC_257, L_MISC_258`

_Text lives in the language bank; not duplicated here._
