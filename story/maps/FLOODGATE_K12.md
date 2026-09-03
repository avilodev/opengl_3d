# Floodgate K-12

## Map brief

**Format:** 6v6 PvP  
**Location:** Kestrel Floodgate 12, Varos Corridor  
**Footprint:** 150m × 150m boundary; the useful combat loop is approximately
120m × 120m  
**Teams:** Bastion enters from the north railhead. Axiom enters from the south
triage annex.  
**Design target:** compact fights in the middle, with specialist movement spaces
on either flank and enough cross-links that no route becomes a tunnel.

K-12 is old flood infrastructure that Bastion expanded into a rail-transfer
station. After contaminated storm water threatened the inhabited corridor,
Axiom installed a temporary biological-filtration annex on the opposite bank.
The temporary facility proved that the filters could support the Meridian
Process. Bastion sealed the railhead; Axiom fortified the annex; the floodgate
became a front line without either company admitting who fired first.

The place should feel wet, loud and under permanent emergency lighting. Bastion
surfaces are heavy plate, amber paint, exposed fasteners and freight markings.
Axiom's end is pale composite, cyan light, sealed medical panels and translucent
screens. Between them is older grey municipal concrete, dark water, red gate
machinery, repair scaffolds and years of flood stains. The current blockout uses
those five color families so navigation already works before bespoke art exists.

## Plan

North is Bastion; south is Axiom.

```text
                         BASTION RAILHEAD
                    [ six protected entries ]
           ┌──────────────┬──────────────┐
           │              │              │
     TURBINES         GATEHOUSE       SPILLWAY
   close / vertical   compact / mixed   fast / exposed
     ║  ║       ┌──── pump control ───┐  ║    ║
     ║  ║───────┤ housings + baffles ├──║ p  ║
     ║  ║       └─────────────────────┘  ║    ║
   roof links          cross-links       ║ p  ║
           │              │              │
           └──────────────┴──────────────┘
                     [ six protected entries ]
                           AXIOM TRIAGE
```

The footprint is rotationally symmetric for competitive fairness, not mirrored
left-to-right. A 180-degree rotation gives each team the same travel distance,
cover, height access and exposure. The two flanks remain mechanically different
from one another, so route choice is meaningful.

## Openings and timing targets

All figures are blockout targets to validate in multiplayer, not immutable
balance promises.

- A straight sprint reaches first Gatehouse cover in roughly 5–6 seconds.
- A committed movement chain reaches the outer edge of mid in roughly 4–5
  seconds, but arrives exposed and audible.
- First sight of the opposite entry is impossible from either spawn deck.
- Crossing from Turbines to Spillway through mid should take roughly 5 seconds
  while moving well, allowing a flank to answer pressure without instant
  teleport-like rotations.
- Returning to the protected part of an enemy entry should require passing one
  of three choke thresholds. It should never be reachable from a single long
  grapple out of mid.

Six spawn slots form two staggered rows rather than a firing line. The central
players have the shortest Gatehouse route; outside players naturally peel into
the specialist lanes. Blast walls hide the first decision from the enemy while
leaving teammates visible to one another.

## Route identities

### Gatehouse — compact competitive core

Two five-metre pump housings split the floor into short engagements. The red
nine-metre control block is the strongest central perch, but it has four exposed
faces and no protected retreat. Low baffles support peeking and slides without
becoming permanent head-height cover. Short mantle steps connect floor, pump
roof and cross-link roof.

Gatehouse is where an uncoordinated team naturally collides. It favors clean
gunplay, fast target transfer and short vertical transitions rather than a long
movement chain.

### Turbines — close and vertical

Broken parallel walls form five- to six-metre gaps: wide enough to fight in,
narrow enough to gain height through wall-jump chains. Segmentation provides
side exits, prevents a future grenade from sealing the entire route, and makes
the wall-run rhythm a series of commitments rather than one automatic rail.

The outer plant roofs are deliberately low. They let a player reset, reload and
re-enter, but do not overlook either spawn. Turbines should produce ambushes and
rapid changes of elevation, with the shortest average sightlines on the map.

### Spillway — movement playground

The paired seven-metre walls are twelve metres apart. That is a skill-gated
lateral wall chain at speed, not a free vertical elevator. A low divider rewards
sustained slides; three tall pylons sit inside grapple range of one another.
Broken bridge halves provide crossing and height without creating a continuous
catwalk from which one player can own the whole lane.

Spillway is the fastest route and the least forgiving. It exposes players to
longer shots from its ends, so speed is defense. A player who stops in the
channel has chosen the wrong route for their intent.

## Cross-links and flow control

- Each specialist route has at least two exits into Gatehouse and one outer
  reset position.
- The central high block cannot see the protected spawn decks.
- The tallest grapple pylons advertise Spillway from anywhere in the map but
  are too narrow to be comfortable firing platforms.
- The perimeter wall is traversable as an expert route, but it is exposed,
  distant from cover and slower to rejoin a fight. It is a comeback flank, not
  the default lane.
- Rotational symmetry prevents one team from owning the intrinsically faster
  direction through a specialist route.

## Callouts

Keep callouts short enough for voice chat:

- **Railhead** — Bastion entry
- **Triage** — Axiom entry
- **Gatehouse** — central complex
- **Red Control** — central high block
- **North Pump / South Pump** — the two central housings
- **Turbines** — west specialist lane
- **North Plant / South Plant** — outer turbine roofs
- **Spillway** — east specialist lane
- **North Bridge / South Bridge** — broken channel crossings
- **Gauge** — central spillway pylon
- **North Link / South Link** — mid-height roofs between lanes

## Art and sound direction

The map should eventually communicate ownership without painting each half in a
team-color wash:

- Bastion: freight serials, crane rails, repair welds, warm sodium work lamps,
  hazard striping, armored shutters and the hammer-in-gear mark.
- Axiom: decontamination tents, medical seals, filter cartridges, white task
  light, cyan status strips and temporary composite cladding.
- Municipal centre: old Varos flood authority plaques, tide gauges, algae,
  mineral lines, dripping expansion joints and red mechanical lockouts.
- Sound landmarks: turbine thrum west, gate alarms mid, rushing water east,
  train brakes north and filtration pumps south. Each lane should be locatable
  with no HUD.

## Blockout validation checklist

Test with twelve humans before adding decorative geometry:

- No opening spawn-to-spawn damage.
- Neither team reaches Red Control materially earlier.
- Every lane supports two-way traversal; no route silently favors one heading.
- A player can leave every long wall run sideways.
- Spillway speed is strong but can be read and punished at its exits.
- Turbines does not become an untrackable vertical maze.
- Four players can fight in Gatehouse without collision geometry feeling
  congested; eight players should create pressure and encourage a flank.
- From each death location, the killer's plausible route can be understood from
  geometry and landmarks.

## Implementation boundary

The blockout lives in `src/gameplay/arena.c`. Geometry, collision, grapple
raycasts and bullet occlusion all consume the same boxes. Team spawn sets also
live with the map through `arena_team_spawns`, and `src/gameplay/spawn_select.c`
is what reads them: it takes a team and the live bodies and returns the slot
whose nearest live body is furthest away. Entering the map and re-entering it
after a death go through the same call, so no coordinate is ever named outside
this file. The current executable is on Bastion only because team selection and
networking do not exist yet.

That rule is distance and nothing else. It has no notion of sight lines, no
spawn protection, and no opinion about which body is an enemy - today every body
in the map is a calibration mannequin. All three want the twelve-player
validation below before they are worth writing.

The standing bodies remain calibration mannequins for hit and occlusion tests.
They have no AI and are not part of the intended 6v6 population.
