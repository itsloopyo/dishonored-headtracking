# Changelog

## [Unreleased]

### Fixed
- A zoom no longer amplifies head tracking. When the game narrows the field of view for a
  scripted scene, the same head turn used to sweep the view much further than it does in
  free play, because a narrower view puts more screen behind every degree. The head is now
  scaled by exactly what the zoom multiplied it by, so a turn moves the picture the same
  distance at any field of view. Leaning is scaled with it; roll is not, since a tilt turns
  the picture by its own angle whatever the FOV. It only ever scales down - a field of view
  wider than the game's default leaves the head at 1:1 rather than amplifying it - and a
  configured `Fov` becomes the baseline, so your own choice of field of view is never read
  as a zoom.
- Head roll now tilts the view the way you tilt your head. The engine boundary negated
  the tracker's roll, on the assumption that it arrives mirrored the way it does in most
  engines, and in game the view leaned the wrong way. Confirmed against the running game.
- Head tracking no longer switches itself off part-way through a session. The guard that
  decides whether an engine pointer is safe to follow took its upper bound from a
  hard-coded 2 GB constant, but Dishonored is linked large-address-aware and its
  allocations run past that line once memory fragments. Every pointer above it was
  treated as garbage, which silently disabled the camera gate, stopped the menu gate
  suppressing tracking in menus, and anchored the FOV override to the wrong value. The
  bound now comes from the OS.
- The view no longer flips upside-down and backwards when you pitch your head up while
  already looking steeply up in camera-local yaw mode. The quarter-turn limit was applied
  on one of the two yaw paths only; it now bounds the head's contribution on both.
- The crosshair no longer snaps back to the centre of the screen when a head turn takes
  the aim outside the frame. It could previously assert the shot was landing dead ahead
  while it landed up to 180 degrees away; it now pins to the edge of the rendered image
  in the direction the gun is pointing.
- A negative position limit in the INI is rejected rather than inverting the clamp, which
  used to pin the camera at a fixed offset that no longer answered the tracker at all.
- The mod no longer risks hanging the game on unload. The old teardown ran under the
  loader lock, where joining its own threads and un-hooking cannot complete; the module
  is now pinned for the life of the process, which is what an ASI plugin wants anyway.
- The INI is no longer resolved against the process working directory when the mod's own
  directory cannot be determined. It failed silently there, so edits next to
  `dinput8.dll` were ignored while the log reported the config as loaded.
- The mod now starts when the game is installed under a path containing characters the
  system's ANSI codepage cannot represent. The config layer is ANSI, and the narrow path
  it was given had every such character replaced with `?`, so the INI resolved to a
  directory that does not exist and the mod gave up reporting that the game folder was
  not writable. The directory is now read wide and, where it cannot be represented
  narrowly, converted through its 8.3 short name; if that is unavailable too, the mod
  says so instead of writing the config somewhere the player will never find it.
- A failure to start the hotkey thread is reported instead of terminating the game.

### Changed
- The vendored Ultimate ASI Loader no longer carries the three third-party DLLs the
  upstream 32-bit build embeds as resources. `binkw32.dll` (RAD Game Tools' Bink and
  Smacker 1.994i, proprietary middleware licensed per title), `wndmode.dll` (DirectX
  Windower Embedded, (C) 2008 VEG and (C) 2004 menopem, no licence) and
  `vorbisfile.dll` (Xiph.Org, BSD-3-Clause) ride along so that a user who renames the
  loader over one of those libraries still gets the original exports, and the installer
  ZIP ships that binary, so it was redistributing all three.
  `scripts/strip-loader-payload.ps1` now zeroes them, `pixi run update-deps` runs it on
  every refresh, and `pixi run package` refuses to build a ZIP from a loader that still
  has them. Only the `.rsrc` section changes: the loader's code, imports, relocations and
  appended PDB are byte-identical to upstream, and nothing in this mod could reach the
  stripped resources anyway.
- `InvertRoll` now ships off, like `InvertYaw` and `InvertPitch`. Roll is mirrored
  relative to UE3, but that conversion belongs at the engine boundary next to the
  position axes rather than in a key that reads as a user preference: a player who turned
  the three Invert keys off used to get a mirrored roll with nothing to explain it. The
  rendered roll is unchanged.
- `DataFreshnessMs` has been removed from the INI. It was written, parsed, and read by
  nothing, so changing it did nothing and said nothing.
- The installer ZIP is roughly half the size. It carried a second copy of the ASI loader,
  zipped, that the manifest never referenced.
- The launcher now preserves an existing `dinput8.dll` as a backup before installing over
  it, and restores it on uninstall, so another mod's proxy is no longer lost. The two
  install routes still differ in how they get there: `install.cmd` detects an existing
  loader and skips installing its own, leaving the incumbent in place to chainload.

### Added
- Leaning no longer pushes the camera through walls. Each rendered frame the mod traces
  the lean it is about to apply against the world's collision geometry, through the same
  `UWorld::SingleLineCheck` the game's own `Trace()` uses, and stops the camera short of
  the first surface in the way. The stop distance is measured along that surface, so an
  angled wall holds the camera as far off as a flat one, and it depends on the world
  rather than on how far the head has moved - so pushing harder against a wall holds the
  view still instead of creeping it forward. When the obstruction clears the view returns
  to the real head position over about a fifth of a second, so walking out from behind a
  doorframe mid-lean does not snap. New `[Collision]` section in the INI: `Enabled`
  (`true`) and `Margin` (`20` world units). The trace only reads the world, and it runs
  on the render path alongside the rest of the injection, so nothing the game simulates
  and nowhere a shot lands is affected.
- `[Camera] Fov`: render the scene at a field of view of your choosing, in degrees,
  measured horizontally at the game's default zoom. Dishonored has an FOV slider of its
  own in Options > Graphics, so this is for the range that slider does not reach, and it
  is off (`0`) by default. The mod detours `APlayerController::GetFOVAngle` and changes
  the answer only for the call `ULocalPlayer::CalcSceneView` makes when it builds the
  projection matrix, so the FOV the game reads for its own purposes - weapon zoom,
  camera modifiers, script - is untouched. What it applies is the difference between the
  configured value and the camera's DefaultFOV, so the game's own zooms still remove the
  same number of degrees they always did, and the crosshair is projected with the same
  number the projection matrix was built from.
- `[General] MoveCrosshair`, default on: the game's own crosshair moves to the point the
  shot lands. Head tracking moves the view without moving the aim, so the crosshair the
  HUD draws at screen centre no longer marks where the bolt goes. The mod writes the aim
  point into the two floats the HUD already uses to place `_root._dot_mc` - the Scaleform
  clip that carries the crosshair art - immediately before the HUD's per-frame crosshair
  update reads them, so the position lands on the frame it belongs to and the game's own
  easing walks the crosshair back to centre when tracking stops. The player sees one
  crosshair, the game's own, sitting where the shot lands, and it is placed with the same
  matrices the injection used rather than a re-derived Euler formula, so it cannot drift
  out of agreement with the camera on combined head poses.
- Head tracking now stops in the main menu and the pause menu. The gate asks the game's
  own UI manager whether either menu movie is open - the same question its per-frame
  dispatch asks before routing to each menu's handler - rather than inferring it from a
  heap-diffed flag. It fails open: if the lookup does not resolve, tracking keeps
  working rather than switching off with no visible cause.

### Changed
- Head tracking is now injected into the rendered view ONLY, which decouples look
  from aim. The mod hooks `APlayerController::GetPlayerViewPoint` and adds the head
  pose to its out-params only when the caller is `ULocalPlayer::CalcSceneView`;
  weapon fire, interaction traces, AI vision and audio call the same function from
  their own sites and keep the rotation the mouse chose. It replaces the previous
  hook on the camera's POV cache, which every one of those systems read too, so
  bolts and bullets followed the head.
- Because the POV cache is left untouched, the renderer's culling and streaming see
  the viewpoint the game expects. That removes the post-cutscene void-black, where
  injecting in the intro's scripted spots rendered the 3D scene as a black void.
- Head tracking now stays live through cutscenes, takedowns, death cams and screen
  fades; menus are the only state it is suppressed in. A scripted camera still renders
  through the same viewpoint, so the player can look around while it drives where the
  view is anchored, and because only the scene view is injected into, the scene itself
  plays out identically whether tracking is on or off. The camera-anim and fade gate
  signals are gone, and with them the per-build PCOwner vtable RVA the anim counts were
  read behind.
- The gate now reads its signals off the controller the scene view asked
  for the viewpoint, rather than searching the heap for a camera object, so there is
  no cached pointer to go stale across a level load.
- The aim marker hides itself once it leaves the frame instead of submitting geometry
  thousands of pixels outside it, and it now gives up on the projection at 84 degrees
  off the view rather than at 89.94, where the divide has already blown the position
  out to five figures.
- The one-line reticle diagnostic now reports the lean the hook applied alongside the
  direction, the field of view and the resulting pixel position, and it fills those in
  before the bail checks rather than after. The marker is rotation-only, so its
  residual error is the lean divided by the target distance; having both on the same
  line makes that arithmetic instead of a hunt, and a bail no longer reports the last
  frame that worked.
- Smoothing is now two user-configurable INI keys under `[Smoothing]`:
  `LocalSmoothing` (default `0.0`) for a tracker running on this machine
  (loopback) and `RemoteSmoothing` (default `0.15`) for a tracker on a remote
  network device. The value is selected per connection from the packet source
  address and re-evaluated every frame, so switching trackers needs no restart.
- The log file is now `HeadTracking.log` (was `DishonoredHeadTracking.log`),
  matching the name used across the mods.
- The log now keeps one previous generation. Each launch truncates the log and
  renames the outgoing `HeadTracking.log` to `HeadTracking.prev.log`, so a
  session never grows past its own launch and a crash report written on the way
  down survives the relaunch that follows it. A rename that fails is reported in
  the fresh log, so a stale `.prev.log` is never mistaken for the last session.
- The camera probe reports when its distinct-reader count changes instead of on
  a 2-second timer, so a 30-second run writes a handful of lines rather than a
  running commentary.
- The log now states the port the UDP receiver bound to on success, not only on
  failure, so "is the receiver up" is answerable from the log alone.

### Fixed
- The crosshair is now placed inside the rectangle the scene view is actually rendered
  into. A camera that constrains the aspect ratio makes UE3 build the projection from
  `ConstrainedAspectRatio` and centre the view inside the viewport with bars; the
  projection previously assumed the view always filled the viewport, which put the
  crosshair in the wrong place whenever it did not.
- Looking far up or down and then nodding no longer flips the world upside-down and
  backwards. The head pitch was added to the game's pitch with nothing bounding the
  sum, and past a quarter turn the engine's rotation matrix mirrors the forward axis
  and inverts the up axis. Common in this game, where you spend a lot of time looking
  at ledges and at your own feet.
- Leaning in now gets the generous 0.40 m of travel and pulling back the restricted
  0.10 m, instead of the other way round. The X and Z axis conversion was being done
  inside the pose pipeline, ahead of the directional limits, which swapped which
  physical direction each limit guarded. It is now done once at the engine boundary,
  after the limits, and the `InvertX` / `InvertZ` INI keys default to false.
- Leaning forward while looking down no longer drives the camera into the floor. The
  lean was applied along a forward vector that carried the camera's pitch, so at 60
  degrees down a 20 cm lean moved the eye 17 cm straight down. The basis is now
  horizon-locked, which is also what makes leaning follow the body rather than the
  head-turned view.
- `LimitY` now bounds movement in both directions. Only the upward bound was wired up,
  so raising it grew the upward budget while the downward one silently stayed at the
  default.
- The aim marker no longer approximates the lean parallax with a fixed anchor distance.
  That made it exact at one range and wrong either side, with the error changing sides
  as the player crossed it, which reads as a broken reticle. It now projects the clean
  aim direction only, which leaves an error of about lean/distance - always the same
  side, shrinking with range - until a live per-frame distance to the surface the bullet
  stops on is available.

### Removed
- `src/probe.cpp`, the hardware-breakpoint camera-discovery probe. It existed to find
  the camera object by scanning the heap for a class vtable; the render-path hook
  reaches the viewpoint through the engine's own call chain and never searches.
- The `aim_decoupling` config field. Nothing read it, and aim decoupling is now a
  property of where the mod injects rather than something to switch off.
- The `[Smoothing] DeadzoneDeg` key. Pose shaping belongs to the tracker, not to each
  game's mod, so that one profile behaves the same everywhere. Set a deadzone in
  OpenTrack or your phone app instead.
- Mod-side recentring. The tracker app owns the centre, so the mod now applies the
  pose it receives as absolute. The `Home` / `Ctrl+Shift+T` binding and the
  `[Hotkeys] Recenter` and `[Hotkeys] ChordRecenter` INI keys are gone. Centre in
  your tracker app instead.
- The single `[Smoothing] Smoothing` key, the separate `[Position] Smoothing` key,
  and the hidden 0.15 baseline floor. The two new keys cover rotation and position,
  so local users get zero-latency tracking by default.
- The per-transition `GATE` dump from the camera hook. It restated the gate
  predicates on every state change while the latched "injection suspended" /
  "injection resumed" lines already say what changed.

## [0.0.0] - 2026-06-03

### Added
- Initial scaffold. C++/ASI head-tracking project skeleton for Dishonored (Unreal Engine 3, Win32), installed via an Ultimate ASI Loader dinput8.dll proxy. OpenTrack UDP receiver, hotkey poller, INI config, and PE-fingerprint logging.
- Camera hook: MinHook detour on ACamera::GetCameraViewPoint (RVA 0x1BD000) injecting head rotation (UE3 int32 FRotator) and a 6DOF position offset (applied in the camera's clean-orientation basis) into the per-frame render viewpoint. Append-only build-profile registry keyed on PE fingerprint (steam-win32-20220217); the mod stays dormant on any unrecognised build.
- 6DOF positional tracking with per-axis sensitivity, limits, smoothing, inversion, and a configurable world-unit scale (cm per metre). Page Up / Ctrl+Shift+G toggles position.
- Controls: Home/End/PageUp/PageDown plus Ctrl+Shift+T/Y/G/H chord alternatives.
- Verified in-game: loader engages, build profile matches by fingerprint, hook installs, OpenTrack data flows, no crash. Aim is currently coupled to the view (shared GetCameraViewPoint chokepoint); render-only decoupling is the next iteration.
