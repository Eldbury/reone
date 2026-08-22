# Save/reload lifecycle regressions

Findings from an evidence-led recce against `85346d5b7` (`[game] Implement the
K1 Galaxy Map (#306)`). Each defect below was reproduced deterministically in
both K1 and K2 with a temporarily instrumented build; the instrumentation is not
part of this branch.

All four defects live in code introduced by `#292` (`[game] Add save and load
support`). None predate it. They stayed invisible to the existing tests because
those load party state from save fixtures, which is exactly the case that works.

## 1. Active party is lost across save/reload

An active companion disappears from the party after a save is reloaded. The
player character survives; the companion does not.

* Active membership *is* serialized correctly. `currentSavedMembers()` emits the
  companion's NPC index into `PT_MEMBERS`, and `parsePartyTable()` reads it back
  into `PersistedState::memberIds`.
* Availability is *not* maintained from runtime state.
  `PersistedState::npcAvailable` has no runtime writer: the only assignment is on
  the load path, from `PT_NPC_AVAIL`. `Party::addAvailableMember` populates the
  runtime `_availableMembers` map only, and `loadDefaultParty` adds active
  members without registering availability at all.
* Because availability is false at save time, no `availnpc<N>.utc` record is
  written. The companion creature is never persisted.
* On reload, `deserializeAvailableNpcs` skips the NPC, so
  `deserializePartyMembers` cannot resolve `PT_MEMBERS` to a creature. It warns
  `NPC is not available: <n>` and drops the member.

Consequently availability can be *loaded* from a save but never *gained* during
play: a companion who joins mid-session is lost on the next reload, while a
session that originated from a save round-trips correctly.

Retail keeps one authoritative flag and enforces the invariant at the join
point: `CSWPartyTable::AddNPC` sets availability and persists the creature, and
`CSWPartyTable::AddMember` refuses an NPC that is not available. reone has two
disconnected representations and no invariant tying them together.

Reproduced in K1 (`danm14aa`, companion `bastila`, NPC 0) and K2 (`101per`,
companion `kreia`, NPC 0).

## 2. Retained item-ID namespace collision

After a reload, an ordinary module transition can abort with
`retained module item ID collides in saved object namespace`.

`ModuleObjectIdContext::retainItem` reuses `Item::originalOwnerLocalObjectId()`
as a module-global identifier. That value is owner-scoped by construction — the
provenance record stores an `originOwner` alongside it — so it is only unique
within one owner's `Equip_ItemList` or `ItemList`. Inserting it into the single
flat `_used` set makes it collide with:

* another owner's retained item ID, or
* an ordinary module world-object ID reserved via `reserveWorldId`.

Both forms were observed. The session-owned party player's equipment is fed into
the module namespace by `buildObjectIdContext`, which is how a session-owned
item's retained ID reaches a module-scoped set in the first place.

The collision only appears after a reload because items built from blueprints
carry no retained ID at all: `retainItem` returns early for them and
`allocateItem` assigns unique module-global IDs. Whether it fires is
data-dependent, so it presents as intermittent.

The guard itself is a sound global-uniqueness assertion. It is not detecting
genuine ownership duplication here — the colliding objects are distinct — so it
must not simply be relaxed; the identifier being retained is the problem.

## 3. Runtime-created continuation holding a live Effect cannot serialize

A `DelayCommand`/`DoCommand` continuation can hold an `Effect` on its VM stack.
`exportScriptSituation` accepts only `SavedEffectValue`, which is constructed in
exactly one place — the *import* path in `savedsituation.cpp`. Effects produced
live by the `effect.cpp` routines are concrete `Effect` subclasses and can
therefore never be exported.

The practical effect is that DoCommand/DelayCommand continuations round-trip
only when they were themselves loaded from a save. An originally runtime-created
continuation holding any effect value fails to snapshot, which aborts ordinary
authored module transitions and blocks saving in the affected module.

This is independent of save/reload: it reproduces on a plain transition with no
save involved. The rejected value observed was an `EffectVisualEffect` result
(`EffectType::Visual`) held as a continuation local — an entirely routine retail
construct, not an exotic value.

`Object::applyEffect` already builds an `EffectInstance` from a live `Effect`;
that conversion is simply absent on the VM-stack export path.

Reproduced on the Endar Spire pair (`end_m01aa` / `end_m01ab`, script
`k_pend_1b_area2`) and on the equivalent K2 content.

## 4. Transition snapshot failure is silently discarded

When the source-module snapshot fails, `Game::loadModule` returns `false`. The
scheduled-transition path clears `_nextModule`/`_nextEntry` and returns without
surfacing anything. The player walks into a door or trigger and simply nothing
happens; the only trace is an error line in the log.

This is what makes defects 2 and 3 present as mysterious no-ops rather than
reported failures, and it should be addressed regardless of their fixes.

## Recommended separation

Four independent changes, in this order:

* **C2** — surface transition-snapshot failure instead of discarding it. Cheap,
  and makes the rest diagnosable.
* **A** — party availability round-trip: single-source availability from the
  runtime party, and validate that every `PT_MEMBERS` entry is available and has
  a corresponding `availnpc<N>.utc` at write time.
* **C1** — live `Effect` export: wrap a live effect into an `EffectInstance` on
  the export path, as the apply path already does.
* **B** — item ID namespace: stop retaining owner-scoped IDs into the
  module-global namespace, keeping the uniqueness guard for genuinely global IDs.

Severity: A is the worst outcome (silent, permanent companion loss in real
playthroughs). B and C1 both block transitions and saving. C2 is lower severity
but amplifies all three.

A and B indicate the save-wide/module-scoped ownership boundary is not yet
pinned down, so both are worth resolving before further save/load lifecycle
work builds on it.
