# Dialogue camera metadata evidence (CAM2)

These fixtures contain names and numeric observations only. No game executable,
DLG, model, script, dialogue text, audio or other proprietary asset is included.

## Animation names

`cameraanimations.h` is a literal transcription of the 728 ordinal-to-string
results in `k1-binary-animation-map.json` and `k2-binary-animation-map.json` from
the accepted 2026-09-08 reconnaissance. Both maps have SHA-256
`60465e5d0da15f1f86db21a685b5e51d38db3d0732b83e0d2ca87ca449458d99`.
They were independently re-extracted from the binaries during CAM2; all slots
agree. The fixture is not generated from the production decoder formula.

| Binary | SHA-256 | Jump table (VA) |
|---|---|---|
| K1 Mac i386, local `XKOTOR(9)` | `a181529e031c8e37f827b69175966a7388453963934698f2c524cc6d6b59a3d7` | `0x475920` |
| K2 Mac i386, local `KOTOR2_Mac_i386(41)` | `f14f285b1b6ed7e4bf000fe19ebcd0d6528d1d3e86d2832be6092761557c130d` | `0x547650` |

Extraction: for each ordinal 1000 through 1727, read the little-endian jump
destination at `table + 4 * (ordinal - 1000)`. Disassemble that case through its
first call; record the immediate string argument (`cut...`) or the default
`none` (`0x3c7fac` in K1, `0x527982` in K2). This follows the binary destinations,
not an arithmetic reconstruction of the names.

Bands end at `1127=cut128`, `1327=cut128w`, `1527=cut128l`, and `1727=cut128wl`.
The three intervening 72-slot gaps all map to the literal name `none`.
The broad selection gate is 1000–1727 inclusive, separate from that name map.
Shipped 10098 does not pass that gate.

Both binaries contain four exceptions to otherwise sequential names:

| Ordinal | Mapped string | K1 case / string VA | K2 case / string VA |
|---|---|---|---|
| 1028 | `cut039` | `0x47d80` / `0x3c8094` | `0x25d4db` / `0x5345dd` |
| 1228 | `cut039w` | `0x48800` / `0x3c848c` | `0x25df5b` / `0x534972` |
| 1428 | `cut039l` | `0x49280` / `0x3c8884` | `0x25e9db` / `0x534d6a` |
| 1628 | `cut039wl` | `0x49d00` / `0x3c8cec` | `0x25f45b` / `0x53517e` |

They use the same string pointers as 1038/1238/1438/1638 respectively. There is
no observed `cut029` mapping in these four bands. This corrects the sequential
summary in the original report; it does not create a suffix alias or fallback.

`CGuiInGame::IsLoopingDialogStuntAnimation(unsigned short)` at K1 `0x4a880` and
K2 `0x2653a0` tests the full interval 1400–1727, including 1528–1599 (`none`).
That flag is ordinal metadata, not a test for whether an asset contains a clip.
K1 subtracts 1400 in a WORD and compares unsigned `<=327`; K2 compares the WORD
result `<328`. The decoder is camera-specific; this evidence does not narrow
the participant animation namespace.

## DLG loader data

Primary `CSWSDialog::LoadDialogCamera` evidence: K1 Mac i386 symbol and K2 PE
`0x750110` in the recce. Angle is DWORD (default 0), ID is signed INT (default
0, retained only at angle 6; -1 means absent), animation is WORD, offsets and
FoV are FLOAT (default 0), video effect is signed INT (default -1). DLG FoV
missing/zero/negative has no positive override. Safe rejection of nonfinite,
out-of-range or unrepresentable projection floats is Reone policy; it is not a
claim that vanilla sanitizes malformed files and does not apply to static GIT.

The existing generated DLG schema and `LoadDialogBase` preserve node skip as
INT, fade type as BYTE, fade delay/length as FLOAT, fade color as Vector, and
listener as CExoString. Root `OldHitCheck` is BYTE and abort/end scripts are
ResRefs. CAM2 tests these wire types with synthetic serialized GFFs. Tests run
both EntryList and ReplyList; adding these fields does not execute them.

## Shipped examples

Only the listed camera fields are reproduced in tests. The complete resource
hashes identify the original local observations, not committed assets.

| Game / resource | Location | Observed fields | DLG SHA-256 |
|---|---|---|---|
| K1 `tar02_start`, entry 0 | `modules/tar_m02af_s.rim` | angle 4, animation 1200, FoV 34.5 | `76d7084c64f4d37c9dc75bd90cb491c8a038b349a119820874ebd7df9c3fb022` |
| K1 `m12aa_c06`, reply 2 | `modules/STUNT_44_s.rim` | angle 4, animation 10098, FoV -1 | `7c4856780f996ee534fb8825d897bedd7d097c32a21cf89de4c49fd064513873` |
| K2 `intro`, entries 3 / 4 | `Modules/001EBO_dlg.erf` | angle 4, animation 1200 / 1000, FoV -1 | `ef848780846940eb92d159ef138fb7a1876f2ad10aaa504607b2d4b1bd367973` |

K2 `001EBOcam` contains `CUT001W`, but no `cut001`. Thus entry 4's ordinal 1000
passes the broad gate and maps to `cut001` while the clip is absent. These are
three different facts. Name normalization can be applied during a later asset
lookup; the decoder must not substitute `cut001w`, inspect the model or decide
what missing playback should do.
