# K2 Ebon Hawk boarding - remaining issues

Investigation notes only. Nothing here is a fix, and the three issues below are
separate semantic problems that happen to share a symptom. Keep them separate
unless evidence connects them.

Base: `[script] Implement GetPartyLeader`, which unblocked boarding itself.
That routine is no longer part of this investigation.

## What retail does

Five K2 landing modules - `201TEL`, `301NAR`, `401DXN`, `601DAN`, `701KOR` -
and only those five, ship `tr_ehawk_custom.ncs`. Each places two overlapping
triggers on the Ebon Hawk ramp:

| blueprint | tag | Type | LinkedToFlags | LinkedTo / LinkedToModule | OnEnter |
|---|---|---|---|---|---|
| `newgeneric016` | `to_ebonhawk_custom` | 0 | - | - | `tr_ehawk_custom` |
| `newgeneric015` | `to_ebonhawk` | 1 | 2 | both empty | none |

`newgeneric016` is the larger of the two and encloses `newgeneric015`, so the
player enters the scripted trigger first and the label trigger second.

`tr_ehawk_custom`, decompiled from the shipped bytecode:

```c
void RemoveEveryoneFromParty();

void main() {
    if (GetEnteringObject() == GetPartyLeader()) {
        int nDest = <planet id>;
        SetGlobalNumber("003EBO_BACKGROUND",  nDest);
        SetGlobalNumber("003EBO_RETURN_DEST", nDest);
        SetGlobalFadeOut(0.0, 1.0, 0.0, 0.0, 0.0);
        DelayCommand(1.0, RemoveEveryoneFromParty());
        DelayCommand(2.0, StartNewModule("003EBO", "WP_from_outside", "", "", "", "", "", ""));
    }
}

void RemoveEveryoneFromParty() {
    SetPartyLeader(-1);
    for (int i = 0; i < 12; i++)
        if (IsNPCPartyMember(i))
            RemoveNPCFromPartyToBase(i);
}
```

No child scripts, no dialog, no galaxy map. The target module `"003EBO"` is a
literal in the script - the engine must not supply or hardcode it. What is
dynamic is the return context: `003EBO_RETURN_DEST` and `003EBO_BACKGROUND`,
which `003EBO`'s own `tr_leave_ehawk` reads back on departure.

## A. Missing fade - routine 720 `SetGlobalFadeOut`

`SetGlobalFadeOut` is a generated stub that throws. Because its return type is
void the exception is swallowed and the script continues, so the transition
still happens - but nothing appears on screen.

Retail starts a one-second fade to black the instant the player enters the
ramp, two seconds before `StartNewModule` fires. That fade is what commits the
player. Without it the observed behaviour is: walk onto the ramp, nothing
happens, walk away, and get teleported two seconds later from wherever you now
are.

Confirmed in a live log:

```
WARN Routine not implemented: SetGlobalFadeOut
Action: 00ce SetGlobalFadeOut(0.0, 1.0, 0.0, 0.0, 0.0) -> void
```

This is not cosmetic. Boarding does not read as correct until 720 exists.

Related and also stubbed on the same path, tracked with this issue because it
shares an owner rather than because it shares a cause: routine 846
`RemoveNPCFromPartyToBase`, so the party is not emptied on arrival at the Hawk.
`003EBO`'s departure script calls `ShowPartySelectionGUI` and expects the
emptied party.

Note for whoever implements 720: `Party::onLeaderChanged()` dereferences
`_game.module()` unguarded, and the same script calls `SetPartyLeader(-1)` on a
one-second delay. That is safe with a module loaded but is a latent null
dereference.

## B. Missing transition banner

`Area::transitionPresentationPortals()` skips any trigger whose
`linkedToModule` is empty:

```cpp
if (trigger->linkedToModule().empty() || !trigger->isActive()) {
    continue;
}
```

`to_ebonhawk` has empty link fields by design, so the banner never renders.

`TransitionDestin` is a localized display name, not a destination. Resolved
against `dialog.tlk`:

| strref | text |
|---|---|
| 123845 / 123869 / 123896 / 123912 / 123933 | `Ebon Hawk` |
| 123783 / 124247 | `Leave Ebon Hawk` |
| 128808 | `Board Ebon Hawk` |
| 94236 | `Sith Academy` |

Content census of triggers carrying `LinkedToFlags` with empty link strings -
9 in K2, 2 in K1 - shows this is an established authoring pattern, not a K2
quirk. `262TEL`'s instance is tagged literally `tr_ehawk_label`. `AreaTransition`
is presentation only: it sets `LBL_DESCRIPTION`, `LBL_TEXTBG` and `LBL_ICON`
and never initiates a transition.

Whatever fixes this must not make empty-link triggers act as transitions. Their
inertness is correct.

## C. Post-load hard block

Reproduced twice, both times on a return to `003EBO`.

The transition itself now completes. From a live log, with the per-instruction
channel off:

```
Action: 013b StartNewModule("003EBO", "WP_from_outside", ...) -> void
INFO Loading module '003ebo'
DEBUG Module source mounted: lips:003ebo_loc.mod
DEBUG Module source mounted: modules:003ebo_s.rim
DEBUG Module source mounted: modules:003ebo_dlg.erf
DEBUG Module source mounted: currentgame:003ebo.sav
INFO Load area '003ebo'
```

and then nothing further is ever logged.

Established:

- the block is after `Module::loadArea` logs `Load area`, so it is at or after
  `Area::load`;
- the load path is single threaded - `Game::withLoadingScreen` calls its block
  inline, there is no worker;
- the process is blocked, not spinning: CPU delta measured as exactly 0 over
  5 seconds, twice;
- it is not transition re-entrancy. `scheduleModuleTransition` only assigns
  `_nextModule`/`_nextEntry`, so a duplicate schedule collapses, and
  `loadNextModule()` is driven from the single-threaded update loop;
- `_transitionInProgress` guards nothing but save eligibility, which is worth
  knowing but is not implicated here.

Not established: where it blocks, and whether it is reachable without this
boarding path. The first return to `003EBO` in a session completed fine both
times; it was the second that hung. Do not assume it is pre-existing.

Two obstacles to pinning it down, both worth fixing before the next attempt:

- `Area::load` emits no log lines, so the log cannot narrow the location;
- the logger buffers 512 bytes before flushing
  (`Logger::append`), so on a hard block the tail is lost. A throwaway
  per-line flush makes the last line meaningful.

Next step is a stack from the hung process, or a diagnostic build with per-line
flushing and progress markers through the post-`Load area` path.

## Reproduction

Any of the five landing modules. Observed on `601DAN` and `701KOR`.

```
engine.exe --game "<K2 path>" --dev true --logsev 0 --logch 1537
```

`--logch 1537` is Global + Script + Script2: trigger entries, module loads,
routine calls with return values, and unimplemented-routine warnings, without
the per-instruction trace. Adding Script3 (2048) turns on the instruction trace,
which is what proves gate behaviour but produces tens of MB.

Board the Hawk from a planet, depart, then board a second time - the hang
appeared on the second return.
