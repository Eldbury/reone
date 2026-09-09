# Dialogue camera playback evidence

This extends `camera-animation-evidence.md`; the accepted ordinal decoder and
its four `cut039` mappings remain unchanged. No dependence of shipped content
on those four mappings has been demonstrated. No game assets are included here.

Primary local binaries:

- K1 Mac i386 `XKOTOR(9)`, SHA-256
  `a181529e031c8e37f827b69175966a7388453963934698f2c524cc6d6b59a3d7`.
- K2 Mac i386 `KOTOR2_Mac_i386(41)`, SHA-256
  `f14f285b1b6ed7e4bf000fe19ebcd0d6528d1d3e86d2832be6092761557c130d`.

The original reconnaissance remains at
`/home/myst/reone-camera-recce-20260908/REPORT.md`. Additional disassembly and
metadata extracted during this series are retained outside the source tree at
`/home/myst/reone-camera-series-work/evidence/`.

## Clip lookup and clocks

K1 `FindAnimation` (0x2de4fa) compares names case-insensitively, searches a
supermodel before falling back to literal `default` at the terminal model
(0x2de540–0x2de5b9). There is no suffix alias. `Gob::PlayAnimation`'s clear
command string at 0x3c9fbc is `NULL`, not the decoder's literal `none`.

`Gob::GetAnimation` (0x2de5c6) separates current phase, asset length, and an
existing channel. It reports a length even if the requested animation has not
started. Module `SetAnimatedCamera` consumes that full length, independently of
the returned channel pointer (0xb929d–0xb92c4), and supplies flags 2 for loops
and 3 for one-shots (0xb92dc–0xb934f).

`Gob::PlayAnimation` preserves an existing named channel's time
(0x2e13a9 → 0x2e1476). `Gob::Animate` advances time at 0x2e08c8, clamps a
one-shot after its length, marks it for removal at 0x2e0abd, and removes it on
the following update (0x2e08eb–0x2e0997). Re-requesting an already removed
channel starts a new one. Exact boundary-frame reuse has not been observed
live; holding the final pose safely does not require retaining that allocation.

`IsCameraAnimated` (0xb7ce8) asks for the currently requested **name**, rather
than merely testing whether any camera animation runs. `IsDialogDelay`
(0x2598f9–0x25993e) combines that activity with sound, participant and fade
waits; 0x2599b4–0x2599f7 also checks the world-time deadline. Missing requested
clips must not wait on an unrelated retained clip. A matching looping clip can
continue waiting until skip/interruption. Render success is not suitable as an
authoritative clock or as a substitute for this immutable interpretation.

`InitializeShotCamera` / `SetDialogCamera` do not remove the module's animated
model. Selection registers it and sets always-render/in-cutscene; full
`RestoreCamera` deletes it at 0xbea3e–0xbea5c. Continued animation while inactive
within the same dialogue is strongly inferred from that membership, not yet
verified by a live vanilla A → static → A phase trace.

## Representative shipped metadata

| Game/resource | Observation |
| --- | --- |
| K1 `tar02_start`, `m02af_c10_cam` | Entries 0/1 request 1200/1201, angle 4, FoV 34.5. Clips last 7.6667 / 4.1 seconds. Mixed PLAYER stunts are authored without root AnimatedCut. |
| K2 `intro`, `001ebocam` | Model contains only CUT001W, length 23.6667. Entry 3 requests 1200; entries 4–7 request missing CUT001 via 1000. No W-to-plain alias is justified. |
| K2 `kreiatch`, `301narcam` | Entry 3 requests missing cut002l via 1401 with WaitFlags 3 and Skippable 1. Treating every looping ordinal as an infinite wait would hang this shipped sequence. |
| Both games, 10098 | Outside animated selection, including authored replies. It is not a request for a fabricated W clip. The exact angle-4 fallback pose remains unverified. |

The installed-resource scan finds 231 K1 accepted camera ordinals (194 with
WaitFlags 1, 37 with 9). K2 has 81 nonlooping and four looping requests; the
only looping request with camera-wait bit 1 is the missing `kreiatch` clip above.
These are observations of the examined installations, not format restrictions.

## Controller interpretation

K1 `NewController::GetKeyIndexAndAlpha` (0x2dcbba) and vector evaluation
(0x2de6a6) hold the final key after its time. Reone's pre-series generic track
sampler extrapolates the last segment instead. Bezier vector rows contain
value, incoming handle offset, outgoing handle offset. The segment uses left
value + outgoing offset and right value + incoming offset as control points
(0x2de831–0x2de8d0), with ordinary normalized segment time. Float evaluation
uses the same layout. No invented easing curve is needed.

Shipped `m02af_c10_cam` has a rest camera-hook position of approximately
(91.4608, 144.9450, 1.57062). CUT001W begins with a zero translation controller;
CUT002W begins with (-2.46750, -2.79258, -0.419283). `001ebocam` similarly has a
nonzero rest hook and a zero first animation translation. This supports the
existing additive translation path; replacing it with absolute animation
positions would be wrong for these fixtures.


Participant wait correction (CAM6): K1 IsAnimationPlayingInDialog0x4c4f8..4569 returns ANY active staged participant; K2 same symbol0x26531a..539e returns ALL (early false at0x265380). K1 IsLoopingDialogAnimation0x4f300 and IsFireForgetDialogAnimation0x4ed04 include both table flags and broad participant cut bands. K2 reconstructed code is accurate for this ALL difference, but must not be projected onto K1. This explains companion loops beside a finite actor in K2 WaitFlags4 content. Implement K1 ANY/K2 ALL against current requested participant channels, including loops, while treating an empty list or missing/lost request as nonwaiting (safe divergence from K2 empty-list true/null dereference). No participant namespace narrowing. Actual model/channel state is local today; downstream MP must supply authoritative participant animation activity, not observer rendering. Exact semantic 2DA overlay/current-ordinal equivalence remains a limitation of existing participant playback.

## Framing, audio and presentation effects

`swbehaviorcameradialog.cpp` in the supplied K2 reconstruction, cross-checked
against the K1/K2 binary traces retained with the reconnaissance, supplies the
angle 1/2/3 geometry and OldHitCheck distinction. Tests use independent numeric
frames for their constants, not screenshots as geometric oracles. Angle 0 uses
the recovered 19-shot sequence with a deterministic session start. Exact vanilla
seed/randomness, controller blending and multi-ray collision scoring are not
claimed. The implementation uses walkmesh obstruction, preserves line-of-action
side for up to four pairs, and samples rendered CAMERAHOOK positions after model
animation. OldHitCheck uses rest hook heights; the close-shot branch adds the
first height to both endpoints. It never substitutes TALKDUMMY.

Binary `UpdateSoundEngine` uses forward `-zdir`, up `ydir`, with static-camera
microphone position `cameraPosition - zdir * MicRange`. Entry/reply cleanup clears
this microphone state without releasing the retained camera. Existing Reone
third-person listener position policy is preserved.

K1/K2 `SetComputerCameraShot` differs from cinematic `UpdateShotCamera`: computer
`CamVidEffect=-1` selects videoeffects row 0, while cinematic -1 requests disabled
(-2). Computer -2 explicitly disables the effect; ordinary disable requests
respect the script override flag. Successful animated selection bypasses this
effect dispatcher and retains the current effect. `EnableVideoEffect` sets the
script override; `DisableVideoEffect` clears it. Session teardown restores the
captured local effect policy before end/abort scripts; full Area retirement
clears transient effects. This is safe ownership policy, not a reproduction of
any vanilla effect leak.

Metadata-only shipped row 0: modulation `(1,1.4,2)`, saturation `.15`, scan noise
on. K1 reads unsuffixed columns; K2 PC reads `_pc`, not `_xbox`. K2 row 3
clairvoyance is used extensively by `kreiatch`; it is not treated as an obscure
unused field. Exact noise and K2 dream/Force Sight/fury shader algorithms remain
unverified: the renderer supplies bounded visual approximations, and does not
implement Force Sight alignment silhouettes. Global fade script VM routines and
cross-module global fade handoff are outside this DLG fade implementation.

K1 `computercamera` and K2 `computercam_p` each contain only `LBL_RETURN`
(strrefs 48226 and 48759 respectively). Recovered `SetLiveFeedMessageVisible`
operates that label; effect 10/-2 hide it. Missing static destination returns to
the terminal presentation without deciding dialogue progression.

Fade types 1/2 are immediate out/in (authored length ignored); 3/4 are timed
in/out. Fade delay, length and color are carried into the same logical fade
clock used by wait bit 8. Automatic blank replies apply fade independently of
camera selection. Invalid/nonfinite times become zero and colors are finite
clamped values: this is malformed-input safety, not a vanilla sanitation claim.

K1 `m41ad_c01_cam` contains `ringfade` light/scale controllers. Camera model
render membership is therefore necessary even though most camera trees contain
only dummy nodes. Explicit render-only roots preserve private animation ownership
and normal scene light/mesh collection. Cleanup removes registration and cached
references before releasing mutable nodes. Visible/hidden children do not change
the number of model advances.
