# Sanctum: Mecha-Chivalry — Systems Brief

Everything the game currently does: its screens, its options, its rules, and all 67 cards.
Written to hand to someone who has never seen it and ask what to build next, so every section
ends by naming what is **not** there rather than only what is.

Every number here was read out of the source: card data from `assets/data/cards.json`, rules
constants from `DuelEngine.hpp` and `DeckBuilder.hpp`, win rates from `SanctumDuelSim`.

---

## 1. What it is

A single-player turn-based card duel. You command a mech army built from two of six doctrines
and fight five commanders in a row. Cards deploy onto a board of fixed lanes; combat is
positional; a run ends when your reactor hits zero.

| | |
|---|---|
| **Engine** | C++17 with SFML 2.6.2, statically linked. No game engine, no scripting layer. |
| **Build** | CMake via the Visual Studio toolchain. Full rebuild ~30 s. |
| **Shipped size** | 2.4 MB executable, 13 MB of assets. |
| **Development machine** | Windows 11, **7.7 GB RAM**, 28 GB free disk. A real constraint on any answer involving a heavier toolchain. |
| **Rendering** | Fixed 1280×720 design space, letterboxed onto any window through an `sf::View`. Fullscreen by default. |
| **Data** | All card numbers live in `assets/data/cards.json`; behaviour is keyed by C++ enums. Balance changes need no rebuild. |
| **Tests** | Two suites: a rules suite of unit tests, and a simulator that plays hundreds of AI-vs-AI duels and reports win rates per fight and per doctrine pairing. |

---

## 2. Screen flow

States live on a stack rather than a single slot, so an overlay draws over the live board
behind it instead of replacing it. Only the top of the stack takes input.

```
Menu → Dual-Core Protocol → Campaign Map → Duel → Reward → Scrap Bay ──┐
                                  ↑                                      │  ×5
                                  └──────────────────────────────────────┘
                                                                         ↓
                                                                    Run Over
```

Pushed as overlays, over whatever is underneath: **Settings**, **Card Inspect**.

### The climax — three layers, not one

The end of a duel is drawn by three objects that each own one job, because the
first version had the timeline, the portrait and the atmosphere tangled in one
class and every change to one broke the other two.

| Layer | Owns |
|---|---|
| `EndScreen` | The clock and the typography: strobe, scrim, verdict slam, button reveal. Everything else reads its `elapsed()`. |
| `PortraitRig` | The losing commander's portrait coming apart. |
| `EndGameVFX` | The full-screen atmosphere, and nothing else. |

They are drawn as a sandwich, and the order is the point: `EndGameVFX::renderBelow`
puts the wash, the rays and the fracture *on the board*, then the scrim dims all of
it, then the portrait breaks, then `renderAbove` lays ash, vignette and seal over the
broken frame — so the atmosphere frames the verdict instead of hiding behind it.

**Victory** opens a gold-and-cyan wash, fans fourteen rotating god-ray triangles
(`sf::BlendAdd`) out of the dead reactor, floats gold embers *upward*, and spins a
two-ring, 24-tick seal down onto the verdict. The seal lands at `kSealLand = 2.00f`,
the same frame `EndScreen` slams its headline: one impact, not two.

**Defeat** washes black-red, grows sixteen branching glass cracks out of the reactor
— the splinters extend by a *rising vertex count*, so the glass keeps breaking rather
than fading in — drops ash and cinders, and pulses a four-band vignette on a
heartbeat that is deliberately **slowing**: `rate = max(1.05, 3.0 - time * 0.30)`.
The verdict itself tears, `EndScreen::textTear()` returning a horizontal offset gated
by two beating sines so the glitch is intermittent rather than a steady vibration,
with cyan and red ghosts drawn only on the frames that actually tear.

**A held beat before any of it.** `beginEndSequence` sets `m_hitStop = 0.20f` and
`updateEndSequence` scales `dt` by `0.16` while it runs. It is time dilation, not a
freeze — at a flat stop the sparks already in the air hang motionless and the pause
reads as a dropped frame; at a sixth speed they crawl, which is what sells it.

**No screen capture.** The obvious way to blur or fracture the board is an offscreen
`sf::RenderTexture` of the whole frame. That is ~8 MB of VRAM held for the rest of the
duel to reproduce a picture already on the screen, on a machine with 7.7 GB of RAM.
The cracks are drawn as geometry over the live board instead, which costs a vertex
array.

**The backdrop is a slot, not a requirement.** Each outcome looks for a painting of
its own — `assets/ui/victory_bg.*` and `defeat_bg.*`, four extensions and a couple
of alternate spellings, first hit wins — and draws it cover-fitted under the wash
with a slow 4% push in. The lookup is tried once per outcome per run, so a missing
file costs nothing. With no art at all the effects play over the live board, which
is a complete picture on its own: dropping a PNG in is an upgrade, not a fix.

### Drawing a card

A draw is a flight, not an appearance. The back lifts off the pile at half size,
arcs to its hand slot on a quadratic Bezier, then pinches its X axis to nothing,
swaps to the face and opens back out — 0.60s end to end, staggered 0.11s a card
so a five card opening hand fans out instead of arriving as one shape. While a
card is in the air `DrawFlight::hides` keeps its hand slot empty, or it is on
screen twice.

**The control point sits 150px up, for an arc meant to clear 80.** A quadratic
Bezier only pulls half way to its control point, so the obvious 80 measured as a
34px bow off the player's pile — barely an arc.

**Two engine bugs had to go first.** The opening hand emitted no `CardDrawn`
event at all: `startDuel` dealt four cards straight out of `Commander::draw`, so
the most visible deal in the game was invisible to the presentation layer. And
every other draw emitted one event per *effect* with no card id, so a spell that
drew three cards produced one anonymous event. Both go through
`DuelEngine::drawFor` now, which announces each card separately and names it.

### Landing signatures — one per doctrine

Every deployment used to play the same shockwave, dust and white flash, so a
Vanguard walker and a Valkyrie arrived identically.

| Doctrine | Landing | Shake |
|---|---|---|
| Vanguard | pale steel fissures tearing 150px out, dust | 0.12s |
| Arclight | a plasma shaft from off-screen into a pool of light | — |
| Dragoon | fire thrown flat along the deck as a shock front | 0.18s |
| Overseer | two EMP rings at different rates, static round the cell | — |
| Siege | slow heavy dust, deep cracks | 0.35s |
| Valkyrie | drifting motes, a slow ring | — |

They are recipes over five shared primitives — `groundCracks`, `lightColumn`,
`boltRing`, `feathers`, `emberRing` — not six particle systems, which is what
keeps them looking like the same game. The recipes live in `DeploySignature`
rather than in `DuelState`, and the shake is *returned* rather than applied, so
both can be rendered offscreen and asserted on: the six are checked to be
distinguishable from each other by colour or coverage, and Valkyrie is checked to
be the one landing that asks for no shake at all.

> **Why they are not screenshot-tested.** Catching a half-second landing in the
> running game meant banking four turns of energy, guessing which card in hand
> was an affordable unit, and hoping a capture fell inside the window — three
> attempts running it did not. But the *pictures* caught four things no assertion
> would have: the Vanguard cracks were darker than the battlefield art, so they
> drew correctly and were invisible; at a 78px reach they sat almost entirely
> behind the 112px unit tile drawn on top of them; the light column was a flat
> khaki bar, now three nested wedges with a hot core; and `sf::Lines` are one
> pixel wide, so a fissure read as a scratch and is drawn three times at
> one-pixel offsets. Assert the numbers, look at the pictures.

> **Not there.** No save or resume — a run exists only in memory, and quitting loses it.
> No collection, no meta-progression between runs, and no deck editor.

---

## 3. Options

Settings overlay, reachable from the main menu and from inside a duel. Persisted to
`settings.json`. Changes apply immediately; the window is rebuilt when a display setting changes.

| Control | Type | Range / default | Effect |
|---|---|---|---|
| Master volume | Slider | 0–1, 0.80 | Multiplies both channels. |
| Music | Slider | 0–1, 0.70 | Background track level. |
| Sound effects | Slider | 0–1, 0.90 | Combat and UI level. |
| Enemy turn speed | Slider | 0.5–3.0, 1.0 | Divides the 1.05 s pause between enemy actions. |
| Fullscreen | Toggle | on | Rebuilds the window. |
| Screen shake | Toggle | on | Suppresses every camera shake when off. |
| Battle log | Toggle | on | Legacy flag; the log now lives behind its own button. |
| Commander avatar | Picker | — | Chosen on the Dual-Core screen, stored here. |

> **Not there.** **VSync is a saved field with no UI control** — `Settings::vsync` exists, loads,
> saves and drives the window, but nothing sets it. Also missing: resolution picker (windowed
> size is a stored number with no control), key rebinding, colourblind options, and any language
> other than English.

---

## 4. Commander avatar

The portrait shown on the HUD, on the reactor card, and in the end-of-duel animation.

| | |
|---|---|
| **Pool** | Every image in `assets/avatars/`. Dropping a file in adds it to the picker — no code change, no manifest. |
| **Doctrine default** | A file named after a doctrine (`paladin.jpg`, `siege.png`…) becomes that doctrine's default, so a player who never opens the picker still gets a fitting face. |
| **Resolution order** | explicit pick → file named after the primary core → first file in the folder → legacy portrait. |
| **Stored as** | A bare filename in `settings.json`, not a path, so moving the folder later does not break it. Empty means "follow my primary core". |
| **Cropping** | Square, cover-fit, biased toward the top of the source — avatars are usually full-length figures, and a centred square crop of one is a picture of a belt. |

> **Not there.** Only one avatar file exists (`paladin.jpg`), so all six doctrines currently
> resolve to it. No in-game upload, no crop adjustment, no unlockables.

---

## 5. The run

Reactor health and the deck carry between fights. Winning heals 6, adds one card chosen from
three, and then opens the **Scrap Bay**, where one card may be removed from the deck for the rest
of the run. Losing ends the run — with a Retry option on the defeat screen that replays the same
fight from the state it started in.

| # | Commander | Doctrines | Reactor | Extra titans | AI win rate |
|---|---|---|---|---|---|
| 1 | Relaybreaker Gunline | Siege / Dragoon | 56 | 0 | 91% |
| 2 | The Pale Revenant | Valkyrie / Vanguard | 34 | 1 | 51% |
| 3 | Magistrate Vhal | Overseer / Arclight | 38 | 1 | 29% |
| 4 | Marshal Kaine | Dragoon / Siege | 46 | 1 | 27% |
| 5 | Warden AX-7 | Vanguard / Valkyrie | 50 | 2 | 20% |

The ladder is ordered by **measured** difficulty. `SanctumDuelSim` prints how hard every one of
the thirty doctrine pairings is to face at a fixed reactor, and the five fights walk down that
list; the reactor values are the fine adjustment, not the main dial. Two consequences are visible
in the table: the reactor does **not** climb with the fight number, because the difficulty lives
in the enemy's doctrine rather than its health pool, and the final boss carries the smallest
reactor on the path because Vanguard is the strongest primary in the game by a wide margin.

**Win rates are AI-vs-AI, not human.** They come from `SanctumDuelSim`, which plays 120 duels
per fight with the same greedy AI on both sides. That AI now places its Guards by lane and
focuses fire, but it still never shells the reactor with artillery and has no plan across turns —
so these are a floor, not a prediction. They are the only balance signal the project has.

**Two doctrines are known to be off-balance**, and the sweep says so on both sides of the table:
*Inquisitor* is the weakest primary (it cannot clear 5% against the final fight, and is close to a
free win when faced), and *Vanguard* is the strongest (it beats every opponent pairing and is
almost unbeatable as one). Both need a card-level pass; neither is a ladder problem.

> **Not there.** The map is a list, not a map. Fights run 1→5 in a fixed order with no branching,
> no route choice, no events, no shops, and no difficulty setting. The "Deployment Map" screen
> shows the next fight and a button.

---

## 6. Deck building — the Dual-Core Protocol

A deck is exactly two doctrines. The primary decides your commander passive and is the only core
allowed to field its Titan; the secondary is a tactical splash.

| | |
|---|---|
| **Deck size** | 24 cards. |
| **Primary core** | At least 15 cards. Grants the commander passive. May include its Tier 3 Titan. |
| **Secondary core** | At most 9 cards. Tier 1 and 2, spells and counters only — never its Titan. |
| **Copy limit** | Per card, from `deckCount` in the catalogue: 3 for most, 1 for every Titan. |
| **Validation** | Seven named failure reasons (wrong size, same core twice, foreign card, secondary Titan, too few primary, too many copies) so the UI can say which rule broke. |
| **Assembly** | Automatic, and by CATEGORY rather than in one ordered pass. Every deck is built to a fixed mix — **14 units, 6 spells, 4 counter-protocols** — and each category splits its slots between the two cores in proportion. The previous version filled units to a cap, then spells, then counters, and the primary's quota ran out before the counter pass: decks shipped with 17 units, 7 spells and **zero counters**, which silently removed a whole card category from every game. |
| **Run floor** | A run may never carry fewer than 23 cards, so the Scrap Bay cannot whittle a deck into decking out. |

**Why 24 and not 40.** At forty cards the deck was larger than any duel could draw through, so
which cards you owned barely changed what you saw — every game played the same smear of the whole
pool. At twenty-four, adding or cutting one card is a decision the next duel notices, and fatigue
becomes a real clock in a long game. Shrinking the deck also made the whole campaign harder, in
both directions: a smaller deck draws its bombs far more reliably, and the enemy decks shrank too,
so their titans went from 2-in-40 to 2-in-24. The ladder was re-measured and rebuilt afterwards.

> **Still not there.** **There is no deck editor.** The player picks two doctrines and the game
> builds the 24 cards. Deck agency across a run is now two choices per win — one card in from a
> choice of three, one card out from the whole deck — so eight decisions across five fights. No
> upgrades, no relics, no saved decklists, no import or export.

---

## 7. Combat

Each side has a frontline and a support row of four **fixed lanes**, plus a three-slot counter
zone. Lane position is load-bearing: it decides what a frame can reach and what a Guard protects.

| | |
|---|---|
| **Reactor** | 30 health. Reaching zero ends the duel. |
| **Energy** | Cap rises by one each turn to a ceiling of 10, refilling at upkeep. |
| **Hand** | Opening 4, limit 8, one draw per upkeep. An empty deck deals escalating reactor strain. |
| **Deploying** | Tier 2 may scrap 1 friendly frame to save 2 energy; Tier 3 may scrap 2 to save 4. Either may also be paid for outright. |
| **Advancing** | 1 energy to move a frame from support to frontline. Free with Thruster, or once per turn for a Dragoon commander. |
| **Melee reach** | Must stand in your frontline. Reaches its own lane and **one lane either side**. Clears the frontline in that lane before it can touch the support row behind it. |
| **Melee → reactor** | Only when the enemy board is **completely empty**. |
| **Ranged** | Fires from either row at any lane, takes no return fire and deals none. **Can shell the reactor over anything in the way, at half damage.** Doing so, or firing at all from the support row, leaves it exposed to enemy fire until its next upkeep. Cannot reach a standing enemy support row. |
| **Aerial** | Reaches both enemy rows at any lane, and the reactor. Guard still screens. |
| **Guard** | Screens the frames **immediately left and right** in its own row — they cannot be attacked at all while it stands. It does not protect the rest of the row, and the Guard itself is always a legal target. Walls are built by spacing Guards, not stacking them. |
| **Return fire** | Simultaneous. A defender that dies still swings back, so trading into a bigger chassis is a real risk. |
| **Counters** | Set face-down for 1 energy, up to three. Fire out of turn on their condition. One per window. |

**Halving artillery's reactor damage is a balance patch, not a design choice.** At full damage
the first encounter went from a 90% win rate to 37% in one measurement — a Guard wall that melee
can only chip one lane at a time, with artillery firing over the top of it, was unbeatable. This
is the single number most likely to need retuning.

---

## 8. The six doctrines

Pick two; the primary grants the passive.

| Doctrine | Order | Passive | Effect | Cards |
|---|---|---|---|---|
| **Vanguard** | Iron Shield Knights | Aegis Plating | Your frontline frames take 1 less damage from every hit. | 12 / 31 copies |
| **Arclight** | Radiant Core Division | Overcharge Core | Gain 2 overcharge each upkeep. Every plasma strike spends 1 for +2 damage. | 11 / 30 |
| **Valkyrie** | Wings of the Scrap Yard | Nanite Reclamation | The first frame you lose each turn is rebuilt into your deck instead of being scrapped. | 11 / 31 |
| **Dragoon** | Thunderlance Cavalry | Thruster Assault | The first frame you advance each turn advances for free. | 10 / 27 |
| **Siege** | The Immovable Batteries | Fire Support | Your ranged frames deal 1 extra damage from the support row. | 11 / 30 |
| **Overseer** | Cipher Directorate | Cold Read | Arm counter-protocols for free. Every counter that fires draws you a card and burns the enemy core for 2. | 14 / 38 |

---

## 9. Keywords

| Keyword | Effect |
|---|---|
| **GUARD** | Screens the frames immediately left and right of it in its own row. They cannot be attacked while it stands. The Guard itself is always a legal target. |
| **BLITZ** | Can attack the turn it deploys. |
| **RANGED** | Fires from either row at any lane. Takes no return fire and deals none. Shells the enemy reactor over any blocker at half damage, which leaves it exposed. Cannot reach a standing enemy support row. |
| **AERIAL** | Reaches both enemy rows and the reactor at any lane, and takes no return fire. Guard still screens. |
| **THRUSTER** | Advancing to the frontline costs no energy. |
| **REACTIVE** | Takes 1 less damage from every attack. Stacks with the Vanguard passive on the frontline. |
| **PLASMA** | Ignores plating entirely — reactive armour, Aegis and absorption alike. With an Overcharge Core behind it, each strike spends 1 overcharge for +2 damage. |
| **OVERKILL** | Damage beyond what it takes to scrap the defender carries into the enemy reactor. |
| **SPLASH** | The attack spills half its damage onto the frames flanking the target. |
| **EMP** | Anything it hits is shorted out: cannot act next turn, and burns 1 health each upkeep until destroyed. |

---

## 10. Card catalogue — 74 cards, 201 copies

Behaviour names are the C++ enum values. Each one is a distinct hand-written behaviour in the
rules engine, not a generic effect.

### Vanguard — 12 cards, 31 copies

| Id | Name | Type | Cost | ATK/HP | × | Keywords | Behaviour |
|---|---|---|---|---|---|---|---|
| `vg_shieldwall` | Shieldwall Drone | Unit T1 | 1 | 1/3 | 3 | guard | — |
| `vg_bastion` | Bastion-01 Guard Drone | Unit T1 | 2 | 1/3 | 3 | reactive | — |
| `vg_anvil` | Anvil Escort | Unit T1 | 3 | 2/3 | 3 | reactive | — |
| `vg_pike` | Rampart Pike MK-II | Unit T1 | 3 | 2/4 | 3 | guard | — |
| `vg_castellan` | Castellan MK-III | Unit T2 | 4 | 2/6 | 3 | guard | — |
| `vg_galahad` | Galahad-IV Heavy Knight | Unit T2 | 5 | 3/7 | 3 | guard, reactive | — |
| `vg_behemoth` | Behemoth-Prime Fortress | **Unit T3** | 8 | 6/11 | 1 | guard, reactive | ReflectToRow 2 — on being hit, 2 arc damage to the enemy frontline |
| `vg_holdline` | Hold The Line | Spell | 1 | — | 3 | — | ArmourAllAllies 1 |
| `vg_bulwark` | Bulwark Protocol | Spell | 2 | — | 2 | — | ArmourAllAllies 2 |
| `vg_ablative` | Ablative Plating | Spell | 4 | — | 3 | — | ArmourAllAllies 2 |
| `vg_reactive` | Reactive Armor Protocol | Counter | 1 | — | 2 | — | On enemy reactor attack: BlockAndCrushWeak 3 |
| `vg_lastline` | Last Line Detonation | Counter | 1 | — | 2 | — | On own Titan destroyed: DetonateForTitanAttack |

### Arclight — 11 cards, 30 copies

| Id | Name | Type | Cost | ATK/HP | × | Keywords | Behaviour |
|---|---|---|---|---|---|---|---|
| `pl_scout` | Arclance Scout | Unit T1 | 2 | 3/2 | 3 | plasma | — |
| `pl_squire` | Radiant Squire | Unit T1 | 2 | 2/3 | 3 | — | Deploy: GainOvercharge 1 |
| `pl_lumen` | Lumen Cadet | Unit T1 | 3 | 2/4 | 3 | — | Deploy: GainOvercharge 2 |
| `pl_censer` | Cinder Drone | Unit T1 | 3 | 3/3 | 3 | plasma | — |
| `pl_vindicator` | Vindicator Frame | Unit T2 | 5 | 5/5 | 3 | plasma | — |
| `pl_judicator` | Judicator Citadel Knight | Unit T2 | 5 | 5/5 | 3 | splash, plasma | — |
| `pl_solaris` | Solaris, Grand Lightlord | **Unit T3** | 8 | 9/9 | 1 | plasma | Deploy: DumpOverchargeOnDeploy — vents the whole core across the enemy board |
| `pl_conduit` | Core Conduit | Spell | 1 | — | 3 | — | OverchargeSurge 2 |
| `pl_overcharge` | Reactor Overcharge | Spell | 2 | — | 3 | — | OverchargeSurge 3 |
| `pl_flare` | Plasma Flare | Spell | 3 | — | 3 | — | DamageEnemyFrontline 2 |
| `pl_smite` | Orbital Strike | Spell | 4 | — | 2 | — | DamageEnemyFrontline 3 |

### Valkyrie — 11 cards, 31 copies

| Id | Name | Type | Cost | ATK/HP | × | Keywords | Behaviour |
|---|---|---|---|---|---|---|---|
| `vk_wrench` | Wrench Drone | Unit T1 | 1 | 1/2 | 3 | — | End of turn: RepairWoundedAlly 1 |
| `vk_medic` | Nanite-V Field Medic | Unit T1 | 1 | 1/3 | 3 | — | End of turn: RepairWoundedAlly 2 |
| `vk_salvager` | Salvage Rig | Unit T1 | 3 | 2/4 | 3 | — | Upkeep: DrawCards 1 |
| `vk_shieldmaid` | Shieldmaiden Frame | Unit T1 | 3 | 3/3 | 3 | — | — |
| `vk_valkyr` | Valkyr Lancer | Unit T2 | 4 | 4/4 | 3 | blitz | — |
| `vk_freyja` | Freyja, Silver Wing | Unit T2 | 4 | 3/5 | 3 | — | Field: SpawnScrapDroneOnAllyLoss 1 |
| `vk_hoist` | Salvage Hoist | Unit T2 | 5 | 3/6 | 3 | — | Deploy: DrawCards 1 |
| `vk_arkangel` | Ark-Nine Rebirth Carrier | **Unit T3** | 8 | 5/10 | 1 | — | Deploy: ReviveBestFromScrap |
| `vk_beacon` | Recovery Beacon | Spell | 2 | — | 3 | — | DrawThenRepairIfLosses 1 / 3 |
| `vk_scrap` | Scrap Protocol | Spell | 3 | — | 3 | — | DrawThenRepairIfLosses 2 / 4 |
| `vk_wash` | Nanite Wash | Spell | 3 | — | 3 | — | ArmourAllAllies 2 |

### Dragoon — 15 cards, 41 copies

| Id | Name | Type | Cost | ATK/HP | × | Keywords | Behaviour |
|---|---|---|---|---|---|---|---|
| `dg_scoutbike` | Scout Bike | Unit T1 | 1 | 1/1 | 3 | blitz, thruster | — |
| `dg_outrider` | Outrider Skiff | Unit T1 | 2 | 2/2 | 3 | thruster | — |
| `dg_raptor` | Raptor Jet Lancer | Unit T1 | 3 | 4/2 | 3 | blitz, thruster | — |
| `dg_lancer` | Jet Lancer | Unit T1 | 4 | 3/3 | 3 | thruster | — |
| `dg_stormrider` | Stormrider | Unit T2 | 5 | 4/4 | 3 | aerial | — |
| `dg_zephyr` | Zephyr Skyblade | Unit T2 | 5 | 5/3 | 3 | aerial, thruster | — |
| `dg_zero` | Dragoon-Zero | **Unit T3** | 8 | 8/6 | 1 | blitz, aerial | ExtraAttackPerTurn 1; RetreatAfterKill |
| `dg_wrecker` | Wrecker Drone | Unit T1 | 2 | 2/1 | 3 | blitz, thruster | On destroyed: DamageKiller 3 |
| `dg_ace` | Ashfall Ace | Unit T2 | 4 | 3/4 | 3 | aerial | On kill: GainStatsOnKill 1 / 1 |
| `dg_wing` | Wing Commander | Unit T2 | 5 | 2/5 | 2 | — | Aura: AuraBuffFrontline 1 |
| `dg_strafe` | Strafing Run | Spell | 3 | — | 3 | — | DamageEnemyFrontline 2 |
| `dg_scramble` | Scramble Order | Spell | 2 | — | 3 | — | DrawThenRepairIfLosses 1 / 2 |
| `dg_afterburn` | Afterburn Strike | Spell | 4 | — | 2 | — | DestroyHighAttackEnemy 5 |
| `dg_evasion` | Thruster Evasion | Counter | 1 | — | 3 | — | On ally targeted by removal: RecallTargetToHand |
| `dg_veer` | Veer Off | Counter | 1 | — | 3 | — | On enemy advance: WeakenAdvancingUnit 1 / 1 |

### Siege — 11 cards, 30 copies

| Id | Name | Type | Cost | ATK/HP | × | Keywords | Behaviour |
|---|---|---|---|---|---|---|---|
| `sg_crew` | Loader Crew | Unit T1 | 1 | 1/2 | 3 | — | — |
| `sg_spotter` | Spotter Array | Unit T1 | 2 | 1/3 | 3 | — | Field: AuraRangedBonus 1 |
| `sg_mortar` | Mortar-03 Walker | Unit T1 | 3 | 1/3 | 3 | ranged | — |
| `sg_flak` | Flak Turret | Unit T1 | 3 | 1/4 | 3 | ranged, splash | — |
| `sg_howitzer` | Howitzer Frame | Unit T1 | 4 | 2/4 | 3 | ranged | — |
| `sg_ballista` | Ballista Energy Knight | Unit T2 | 4 | 3/4 | 3 | ranged, overkill | — |
| `sg_battery` | Battery Array | Unit T2 | 5 | 3/5 | 3 | ranged | — |
| `sg_leviathan` | Leviathan Cannon Platform | **Unit T3** | 10 | 6/10 | 1 | ranged | End of turn: DamageEnemyBackline 4 |
| `sg_entrench` | Entrenchment | Spell | 2 | — | 3 | — | ArmourAllAllies 2 |
| `sg_barrage` | Suppressing Barrage | Spell | 3 | — | 3 | — | DamageEnemyFrontline 2 |
| `sg_orbital` | Orbital Bombardment | Spell | 4 | — | 2 | — | WipeLowHealthUnits 2 — hits both sides |

### Overseer — 14 cards, 38 copies

| Id | Name | Type | Cost | ATK/HP | × | Keywords | Behaviour |
|---|---|---|---|---|---|---|---|
| `iq_probe` | Recon Probe | Unit T1 | 1 | 2/2 | 3 | — | Deploy: DrawCards 1 |
| `iq_cipher` | Cipher Mobile Hacker | Unit T1 | 2 | 2/3 | 3 | — | Deploy: RevealEnemyTrap |
| `iq_jammer` | Static Jammer | Unit T1 | 3 | 3/5 | 3 | emp | — |
| `iq_surge` | Surge Lance | Unit T1 | 4 | 4/4 | 3 | emp | — |
| `iq_warden` | Grid Warden | Unit T2 | 4 | 4/5 | 3 | emp | BuffPerArmedCounter 1 — +1 attack per counter-protocol you have armed |
| `iq_centurion` | EMP-Centurion | Unit T2 | 4 | 4/5 | 3 | emp, reactive | BuffPerArmedCounter 1 — +1 attack per counter-protocol you have armed |
| `iq_deusex` | Deus Ex-Machina Core | **Unit T3** | 8 | 6/9 | 1 | — | Field: SeizeUnitOnTrapFlip — hijacks an enemy frame whenever your counter fires |
| `iq_purge` | Data Purge | Spell | 3 | — | 2 | — | DestroyHighAttackEnemy 5 |
| `iq_trace` | Signal Trace | Spell | 3 | — | 3 | — | DrawThenRepairIfLosses 2 / 3 |
| `iq_blackout` | Grid Blackout | Spell | 3 | — | 3 | — | DamageEnemyFrontline 2 |
| `iq_firewall` | Firewall Blackout | Counter | 1 | — | 3 | — | On enemy spell: NegateSpellAndBurn |
| `iq_overload` | Overload Virus | Counter | 1 | — | 3 | — | On enemy Tier 2+ deploy: OverloadSummon |
| `iq_snare` | Grid Snare | Counter | 1 | — | 2 | — | On enemy advance: WeakenAdvancingUnit 2 / 2 |
| `iq_blackice` | Black Ice | Counter | 1 | — | 3 | — | On enemy reactor attack: BlockAndCrushWeak 2 |

---

### Dragoon is the strongest doctrine, and it is structural

The opponent sweep is unambiguous: the five hardest rows in the whole table are
Dragoon's five. Its **best** row is about equal to every other doctrine's worst.

| Doctrine as an opponent | Range (lower = harder to face) |
|---|---|
| Siege | 26–61% |
| Vanguard | 33–50% |
| Arclight | 27–45% |
| Valkyrie | 23–51% |
| Overseer | 30–44% |
| **Dragoon** | **20–33%** |

The cause is the lane rules. Melee has to clear its own lane and the one either
side; Guard screens its neighbours. **Aerial ignores all of it** and lands in the
support row, and Dragoon is the aerial doctrine. Every rule that makes the board
matter makes Dragoon matter less — that is a design consequence, not a statline
to shave.

### Five cards added, and why they were needed for a bigger deck

Dragoon had **two spell copies** and **no unit with an ability at all** - the
only doctrine like either. At a 40-card deck the builder asks a primary core for
seven spell copies; Dragoon could supply two, so it was the one doctrine that
structurally blocked a larger deck.

| Card | | Uses |
|---|---|---|
| `dg_wrecker` Wrecker Drone | Unit T1, 2 energy, 2/1, Blitz | `DamageKiller` — detonates into whatever kills it |
| `dg_ace` Ashfall Ace | Unit T2, 4 energy, 3/4, Aerial | `GainStatsOnKill` — grows with each kill |
| `dg_wing` Wing Commander | Unit T2, 5 energy, 2/5 | `AuraBuffFrontline` — the rest of the line hits harder |
| `dg_strafe` Strafing Run | Spell, 3 energy | `DamageEnemyFrontline 2` |
| `dg_scramble` Scramble Order | Spell, 2 energy | `DrawThenRepairIfLosses 1 / 2` |

Three of those ability kinds were **already implemented in the engine and used by
nobody**, so they cost no engine work and gave the doctrine something other than
another fast body. Every card is priced at or below an existing precedent:
Strafing Run is Siege's barrage exactly, Scramble Order is strictly worse than
Valkyrie's beacon, and Ashfall Ace is under Dragoon's own T2 curve.

Dragoon still got slightly stronger — 23–33% to 20–33% as an opponent — and
trimming Wrecker Drone from 3/1 to 2/1 barely moved it. The cause is not a card:
it is that the doctrine now has a **complete curve** where it used to run out of
spells and pad with surplus units. Fight 4 was re-tuned around that (Kaine's
reactor 40 → 46) and the two middle commanders swapped, because Kaine had become
harder than the fight after him.

### Headroom for a bigger deck

At 40 cards a primary core must supply 17 units, 7 spells and 3 counters.

| Doctrine | Units | Spells | Counters |
|---|---|---|---|
| Vanguard | 19 | 8 | 4 |
| Arclight | 19 | 11 | **0** |
| Valkyrie | 22 | 9 | **0** |
| Dragoon | 27 | 8 | 6 |
| Siege | 22 | 8 | **0** |
| Overseer | 19 | 8 | 11 |

Dragoon is no longer the blocker. **Three doctrines still carry no
counter-protocols at all** — the builder falls back to units, which works, but it
means half the roster cannot use the counter system on its own.

---

## 11. Audio

Sound is looked up **by name**, not by hard-coded path. `AudioCatalogue` indexes
`assets/audio/` once, case-folded and with `-` and `_` treated alike, then
resolves each game event most-specific-first:

| Order | Matches | Example |
|---|---|---|
| 1 | the card's own id | `iq_jammer.wav` — a frame with its own voice |
| 2 | doctrine + event | `IQ_Attack.wav`, `Vanguard_Deploy.mp3` |
| 3 | the global event | `sfx_trap_alarm.wav`, `Victory_Sound.wav` |

Event names accept several spellings each, because the files use several:
`Dead` and `death`, `Titan` and `Titan_Spawn`, `Game-Over-sound` with hyphens.
`Vanguard_TItan_Deploy.wav` resolves despite the stray capital. **Renaming an
author's files is not the fix** — the resolver absorbs the variation instead.

A miss returns nothing and the event is silent. It deliberately does **not**
borrow another doctrine's sample: a Siege frame swinging to a Paladin choir is
worse than silence, and silence shows up in the coverage report where a wrong
sound would not.

**Why this exists.** Before it, every call site asked for `explosion.wav`,
`kiem_chem.wav`, `summon.wav` and `bgm_menu.ogg` — names left over from the
fantasy version of the game. None of those files exist. The whole game ran
silent and nothing reported it. `SanctumAudioTests` now asserts the cues
resolve *and* that SFML can actually decode every file, so a static build
without the mp3 decoder fails the test instead of failing quietly in play.

Music: the intro sting plays once on launch and hands over to the menu loop
through a small one-slot queue that `Engine::update` pumps.

### Coverage today — 16 of 24 doctrine slots

| Doctrine | Deploy | Attack | Death | Titan |
|---|---|---|---|---|
| Vanguard | yes | yes | yes | yes |
| Arclight | yes | yes | yes | yes |
| Valkyrie | — | — | — | — |
| Dragoon | yes | yes | yes | yes |
| Siege | — | — | — | — |
| Overseer | yes | yes | yes | yes |

Frames with their own voice: `iq_jammer`. Drop a file named after any other
card id into `assets/audio/` and it is picked up with no code change.

---

## 12. Menu and campaign map

**The portrait picker lives on the menu.** It used to sit under the doctrine
tiles on the Dual-Core screen, where it read as part of building a deck - which
it is not. Your face is a profile setting, chosen once, so it sits with the other
settings and is not re-confirmed at the start of every run. An empty choice means
"follow whichever doctrine I pick", and the menu says so in words, because
otherwise it looks identical to a picker that is simply broken.

The start button says **NEW GAME**. It said NEW DEPLOYMENT, which named the
fiction rather than the action.

**The campaign map carries information now.** It was a row of five circles, one
panel and two lines of text on a flat black field, with most of the screen empty.
It is now three blocks:

| Block | Shows |
|---|---|
| The road | five nodes on a faint band, the current one breathing so the eye lands on it without reading five names |
| YOUR COMMANDER | portrait, faction, both doctrines, reactor as a gauge, and the deck as a stacked bar with a legend - units / spells / counters |
| ENCOUNTER *n* OF 5 | the enemy's portrait, name, reactor gauge, and a THREAT table: doctrines, what it fields, how many titans |

Both reactors are drawn with the **same** gauge routine, so the two numbers read
as one measurement rather than as a bar on one side and a sentence on the other.
The enemy portrait falls back to the shared one and then to nothing without
leaving a hole in the layout.

---

### The board reads as furniture now

Four questions the player used to have to work out for themselves.

**Where can I put things.** Empty ground stayed empty and a cell appeared only
while a card was already in the air. All sixteen unit cells and both counter
zones are drawn as standing sockets, the frontline carrying a stronger edge than
support, each row captioned in the left margin, and the artwork pushed back under
a veil weighted toward the middle where the mandala is brightest. An empty
counter cell shows a closed latch, so it says what it is for.

**How much energy have I got.** `2 | 2` was accurate and useless — deciding
whether a 3-cost card was affordable meant reading two digits and subtracting,
every time. Ten cells answer it at a glance; the exact figure is kept underneath.

**What am I holding.** The hand fans, the outermost card leaning 4°. The
pointed-at card lifts 45px, straightens, draws at 1.35 and draws last so it is
over the whole fan — at 1.45 it covered the two cards beside it outright.

**Am I done.** When nothing in hand can be paid for and no frame can still swing,
End Turn pulses green. Nothing else in the game is green, so it reads at the edge
of vision, which is where it has to: the player is looking at their hand. The
`[ SPACE ]` shortcut is written under the button.

**The reactor cards moved.** They were centred top and bottom, putting the only
bar that matters at the far end of the board from the portrait, energy and piles
describing the same commander — and carried a second copy of that portrait and
name to compensate. They are pinned to the left column now, touching their own
commander strip and tied to it with a bracket, carrying just the bar and the
number. The column ends at x 284 and the rows start at 374, so nothing moved onto
a lane, and "attack the core" became a drop onto the commander it belongs to.


## 13. Counter-protocols on the board

Your own set counters are **not face down**. They are drawn as cards in the
counter zone — art, name, doctrine tint — and either mouse button opens the full
card in the inspector. A counter you set is not a secret from you; hiding it only
meant remembering what you had put there.

They are marked the KARDS way: the **outer frame is replaced** with the armed
colour (`CardArt::armedBorder()`, a violet) rather than decorated with another
icon, plus an ARMED ribbon across the foot. Status reads at a glance, and the
board gains no new symbols.

The **enemy's** counters stay sealed behind a card back. That is the one thing on
the board hidden from the player, and it is the whole point of a counter.

**Arming effect.** `CombatVFX::armFlare` runs the frame hot for 1.3s — three
nested outlines, the innermost being the colour the card keeps, the outer two a
white bloom that burns off — with forty sparks seeded *along the border* rather
than from the centre, so the light comes from the thing that changed. It holds at
full heat for the first fifth before decaying; an earlier ease-out spent almost
all its brightness inside two frames, which was long enough to be correct and far
too short to see.

`SanctumVfxTests` renders the flare to a texture and reads the pixels back:
that it draws in the armed colour, around the border rather than over the card,
that it is still clearly lit a quarter-second in, and that it is fully gone once
its lifetime runs out. Driving the real game to catch a 1.3-second effect proved
useless — the screenshot lands early, or late, or that turn's hand holds no
counter at all.

---

## 14. Presentation — what is already animated

**Intro.** The game opens on `IntroState`, not the menu: a nine-second opening in
three acts, cut to the beats of `Intro.wav`.

| Time | Act | Camera | Sound |
|---|---|---|---|
| 0.0–2.8s | The ruined sanctum | Ken Burns push 1.00→1.06 held on the glowing tower, top right | — |
| 2.8–3.0s | — | fade to black | — |
| 3.0–5.8s | The vow | slow tilt up the blade to her face (15px rise) | — |
| 5.8–6.0s | — | white flash, 0.14s fall | — |
| 6.0–7.5s | The crusade | 3px marching rumble | — |
| 7.5–9.0s | Title slam | scale 2.5→1.0, alpha 50→255, 12px kick | the roar |

**One sound cue, on purpose.** A bell, a sword draw, a reactor boot and an anvil
hit were all cut: over nine seconds of music four stings read as clutter rather
than punctuation. The roar lands with the title, which is the only beat that
needs marking. The camera still kicks whether or not the file is there.

Three still paintings and one clock — every move is a transform on a sprite, for
the same reason the portrait rig is a puppet. **Shake is added to the sprite
position, never to `sf::View`**; a `setCenter()` here would drop the letterbox
and stretch the board on the frame the menu takes over.

The cue is looked up by name with a dedicated stem first and a stand-in after it
— `{"intro_roar", "Dragoon_Titan_Spawn"}` — so dropping `intro_roar.wav` into
`assets/audio` takes over with no code change.


**Two doctrines renamed on screen only.** *Paladin* names a holy order and
*Inquisitor* a church court, so the player now reads **Arclight** and
**Overseer** — named for what the doctrines actually field, an overcharge core
and a counter-protocol watch.

The rename went into a **new `display` column** in `kRoles`, beside the existing
`key`. `key` is still `"Paladin"` and `"Inquisitor"`, and it is still what
`cards.json` stores in `"role"`, what an avatar file is looked up by, and what
`toString(MechRole)` returns. `displayName(MechRole)` is the one a human reads.
Splitting them meant **no card id, art file, audio file or deck changed**:
`pl_censer` is still `pl_censer`, `Paladin_Attack.mp3` and `IQ_Attack.wav` still
resolve, and every test kept passing untouched. `Avatars` searches both stems, so
`paladin.jpg` keeps working and `arclight.jpg` would too.

`assets/data/enemies.json` was deleted. It held the pre-mecha fantasy roster —
Void Apostle, Cultist Fiend, "heretic legion" — no code had read it since the
conversion, but it was still being copied into the build output and would have
shipped.

**No church.** Every displayed string that read as a cathedral, a holy order or
a clergy was rewritten, at the author's request: the caption is now about the
world sliding toward its end rather than a holy land falling to heresy, the
Paladin faction is the Radiant Core Division rather than an Order, the Inquisitor
faction is the Cipher Directorate rather than a Tribunal, and five cards were
renamed (Acolyte, Temple, Smite, Censer, Ark-Angel). **Ids, enums, decks, art and
audio filenames were left exactly as they were** - `pl_censer` is still
`pl_censer`, so its art file, its audio lookup and every test keep working.

**Two names the lookup absorbs rather than corrects.** The Dragoon set on disk
is spelled `Dragon_Deploy`, `Dragon_Attack`, `Dragon_Dead`, `Dragon_Titan` — one
`o` short of the doctrine. `dragon` leads that doctrine's alias list, so the
files work as they are; renaming an author's files is not the lookup's job. It
leads deliberately, so the complete newer set beats the single older
`Dragoon_Titan_Spawn.wav` sitting beside it — which the intro still names
directly for its roar.

Casting a spell has its own cue now (`Spell_Activate.mp3`), resolved card-first,
then doctrine, then that shared sting: a `pl_smite.wav` would beat
`Paladin_Deploy.mp3`, which beats `Spell_Activate.mp3`.

### Two faces, split by job

The whole game was set in Georgia, inherited from the fantasy build this grew
out of. Rendering the real strings at the real sizes showed two hard faults and
one soft one:

- **Georgia has no Vietnamese.** Every accented vowel came out an empty box.
- **Georgia's numerals are text figures.** "Bastion-01" rendered "Bastion-o1",
  and the attack and health badges — the numbers read more often than anything
  else on screen — were set in digits that drop below the baseline.
- A serif at 8–12px on a dark ground is the hardest thing a UI can ask of a
  reader, and most of this game's text is at those sizes.

| | Face | Carries |
|---|---|---|
| `Fonts::display()` | Constantia | Titles, commander names, the verdict, the intro — large, read once, carries the character |
| `Fonts::ui()` | Segoe UI Semibold | Rules text, every label, every number |

Semibold rather than regular on purpose: the game paints warm off-white on
near-black nearly everywhere, and a regular weight thins out against that.
Both are Windows system faces, so nothing ships and nothing is licensed; a
`.ttf` in `assets/fonts` (`Display.ttf`, `UI.ttf`) overrides either without code.

`drawLabel` no longer takes a font at all — every one of its fifty call sites is
8–13px, so the choice is made once. It also **cuts tracking as text gets
smaller** rather than leaving it: the labels were set at 2.0–2.6 across the
board, which at 8px pulls a word far enough apart that it stops reading as one.

Three things the change broke, all caught by looking:

- The UI face runs wider than the serif at the same pixel size, so card names
  ran off both edges. The text column went from 0.80 to 0.88 of the card, and a
  name that still will not fit is **condensed to 88%** before being truncated —
  shrinking below 8px would undo the reason for the change.
- Long rules text overflowed the card and ran under the stat badges. Whole lines
  are dropped now and the cut is marked; the full text is one hover away.
- The truncation mark was written `"…"` and rendered as three wrong
  characters. **`sf::Text` reads a `std::string` as Latin-1**, so anything above
  ASCII has to go through `sf::String::fromUtf8`. It is three dots now.

**Captions are Vietnamese and the game's own face cannot spell them.** Georgia is
missing ten uppercase Vietnamese glyphs (Ế Ề Ể Ỉ Ị Ộ Ờ Ở Ứ Ử), so the subtitles
would have rendered with holes. Rather than restyle every screen, the intro loads
one caption face of its own with full coverage — checked with fontTools, not
assumed — and the sources build with `/utf-8` so narrow literals are not read in
the system codepage. Drop `assets/fonts/Caption.ttf` to override it.

The title lands on a soft dark band. The script called for it to sit in the empty
sky at the top of scene three, but that scene has no empty sky — it is dragon,
spires and red cloud across the full width, and dark red type on dark red cloud
vanished entirely.

It is **not skippable** while it plays. Afterwards it holds on
`[ PRESS SPACE TO COMMENCE ]` and moves on by itself if nobody answers.

> **A note on measuring it.** `Engine::run` clamps `dt` to 0.1s so a hitch cannot
> teleport an animation. Saving a 1920×1080 screenshot takes longer than that, so
> every F12 costs the game clock about a fifth of a second. A burst of thirty
> shots stretched this nine-second sequence past twenty and left it apparently
> stuck in act one — the measurement was changing the thing it measured. Capture
> one frame per launch.

Worth listing because "add juice" is a common answer to "what next", and most of it is done.

| | |
|---|---|
| **Combat** | Lunge and recoil, curved targeting arrows, closing crosshair, hit flash, directional impact debris, ranged tracer beams, splash arcs, batched particle explosions. |
| **Deployment** | Hex landing marker, squash-and-spring on landing, stat-badge flare, then a landing signature that differs per doctrine — see below. |
| **Drawing** | Each card arcs out of the draw pile at half size, grows to hand size, then pinches edge-on and opens out face-up in its slot. |
| **Card spotlight** | Spells, counters and Titans slide in from their owner's side, hold ~1.1 s with a hologram scanline, then break into motes that drift to the scrap pile. Deliberately *not* fired for ordinary deployments. |
| **Counter-protocols** | Alarm strobe, in-slot 3D card flip, then the spotlight. |
| **Turn handover** | Full-width sweep banner across the board. |
| **End of duel** | A held beat, then a four-phase sequence: critical glitch, board dim, verdict slam with camera punch, then buttons. The losing commander's portrait comes apart, and a full-screen atmosphere plays behind it — see below. |

> **Not there.** **Five of seven referenced audio files are missing** — all three music tracks,
> plus `explosion.wav` and `summon.wav`. The code calls them and silently does nothing.
> **19 of 67 cards have real art** (Vanguard and Paladin only); the rest show generated
> placeholders. Card frames are four medieval PNGs shared across six doctrines.

---

## 15. Open questions

The things most likely to be worth an outside opinion, in the order they would change the game most.

| Area | Current state | Question |
|---|---|---|
| **Deck agency** | Auto-built; 4 reward picks per run | Is a full deck editor the right addition, or is a draft/roguelike structure a better fit for a 5-fight run? |
| **Run structure** | Fixed linear 5 fights | Branching routes, events, shops — worth it at this length, or does it need a longer run first? |
| **Persistence** | None | Save mid-run, or lean into short sessions and add meta-progression between runs instead? |
| **AI** | Greedy heuristic, lane-blind | How much does a card-game AI need to understand position before its win rates mean anything? |
| **Balance signal** | AI-vs-AI only | What is a workable balance method for a solo developer with no playtesters? |
| **Art pipeline** | Hand-sourced, scripted crop/import | 48 cards still need art. Consistent style at that volume, on a budget? |
| **Guard rule** | Screens left/right neighbours only | Does adjacency-based protection hold up, or does it collapse into "always play Guards in the corners"? |
| **Artillery** | Reactor shelling at half damage | Is a damage divisor the right lever, or should the cost be positional instead? |
