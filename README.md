# Dishonored Head Tracking

![Dishonored running with this mod](https://raw.githubusercontent.com/itsloopyo/dishonored-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Dishonored that moves the view with your head while your mouse or controller keeps aiming, driven by OpenTrack over UDP, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the rendered view; weapon fire, prompts and enemy awareness still follow your mouse or controller
- **6DOF positional tracking** - lean and peek with head position
- **Works with any OpenTrack-compatible source** - a webcam, a phone app, TrackIR, or anything else that sends the OpenTrack UDP pose to port 4242

## Requirements

- Dishonored on [Steam](https://store.steampowered.com/app/205100/) (App ID 205100).
- An OpenTrack-compatible head tracker. [OpenTrack](https://github.com/opentrack/opentrack) is free and supports webcams, phones, and VR headsets.
- Windows 10 or 11. The game is 32-bit, so the mod ships as a 32-bit `.asi`.

## Installation

1. Download the installer ZIP from the [Releases page](https://github.com/itsloopyo/dishonored-headtracking/releases).
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. In OpenTrack, set the output to UDP and send to `127.0.0.1:4242`.
5. Launch the game.

If the installer cannot find your game, point it at the install folder with an environment variable:

```cmd
set DISHONORED_PATH=D:\Games\Dishonored
install.cmd
```

or pass the folder as the first argument:

```cmd
install.cmd "D:\Games\Dishonored"
```

### Manual Installation

The installer places two files in the game's `Binaries/Win32/` folder. To do it by hand:

1. Extract the Ultimate ASI Loader (`dinput8.dll`) from `vendor/ultimate-asi-loader/` into `Binaries/Win32/`. If a `dinput8.dll` from another mod is already there, leave it in place; the loader only needs to exist once.
2. Copy `DishonoredHeadTracking.asi` into the same folder.

The full path is usually:

```
<SteamLibrary>/steamapps/common/Dishonored/Binaries/Win32/
```

The Nexus release ZIP mirrors the game directory, so extracting it into the game folder puts `DishonoredHeadTracking.asi` in `Binaries/Win32/`. `LICENSE`, `THIRD-PARTY-NOTICES.md` and `README.md` travel at its root. It ships no ASI loader, so it is for users who already run one.

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action              | Nav-cluster | Chord          |
|---------------------|-------------|----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G` |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H` |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

## Configuration

The config file is generated on first run next to `dinput8.dll` at
`Binaries/Win32/DishonoredHeadTracking.ini`. This is exactly what it contains:

```ini
; Dishonored - Head Tracking configuration
; Lives next to dinput8.dll in Binaries/Win32/.

[General]
EnableOnStartup=1
Port=4242
; Yaw mode: true = horizon-locked yaw (default), false = camera-local.
WorldSpaceYaw=1
; The game draws its crosshair at screen centre; head tracking moves the view
; away from it. This moves the crosshair to where the game is actually aiming.
MoveCrosshair=1

[Camera]
; Horizontal field of view in degrees, at the game's default zoom.
; 0 keeps the game's own FOV - Dishonored has an FOV slider in Options > Graphics,
; so set this only to go past what that slider offers.
; The difference between this and the game's default is added to whatever FOV the
; game asks for, so weapon zooms still zoom by the same amount, and the crosshair
; is projected with the value the scene is rendered at.
Fov=0

[Sensitivity]
Yaw=1
Pitch=1
Roll=1
; Flip an axis only if your tracker reports it backwards. The engine's own
; sign conventions are already handled; these three ship off.
InvertYaw=0
InvertPitch=0
InvertRoll=0

[Smoothing]
; Chosen per connection from the tracker's source address; covers rotation and position.
; LocalSmoothing: tracker running on this machine (loopback). 0 = none, 1 = heavy.
LocalSmoothing=0
; RemoteSmoothing: tracker on a remote network device. 0 = none, 1 = heavy.
RemoteSmoothing=0.15

[Position]
; 6DOF positional tracking. PositionScale = world units (cm) per metre of head translation.
Enabled=1
SensitivityX=1
SensitivityY=1
SensitivityZ=1
LimitX=0.3
LimitY=0.2
LimitZ=0.4
LimitZBack=0.1
PositionScale=100
; As above: only for a tracker that reports an axis backwards. Leaving these
; off is what keeps LimitZ on leaning in and LimitZBack on pulling away.
InvertX=0
InvertY=0
InvertZ=0

[Collision]
; Head tracking moves the camera off the player's eye, so leaning toward a wall
; can carry the view through it. This traces the lean against the world and
; stops the camera short of whatever it would have entered.
Enabled=1
; World units (cm) kept between the camera and the surface it stopped at.
; Raise it if you can still see through a wall you lean into; lower it if the
; camera stops further from walls than you want.
Margin=20

[Hotkeys]
; Virtual-key codes. Defaults: End (toggle), Page Up (cycle tracking mode), Page Down (yaw mode).
Toggle=0x23
CycleMode=0x21
YawMode=0x22
; Chord alternatives: Ctrl+Shift+Y (toggle), Ctrl+Shift+G (cycle tracking mode), Ctrl+Shift+H (yaw mode).
ChordToggle=1
ChordCycleMode=1
ChordYawMode=1
```

Booleans are written as `1` and `0`; `true` and `false` are also accepted when you
edit the file. Put a comment on its own line above the key rather than after the
value: the parser hands the whole text after `=` to the value reader, so a trailing
`; note` makes a boolean or text setting fall back to its default without saying so.

**Sensitivity and the position limits are not range-checked.** Only NaN and infinity
are rejected, and the log names any value it had to replace. `1.0` is the shipped
sensitivity and the tracker is meant to own pose shaping, so reach for OpenTrack's
own curves before these. A position limit must be greater than zero; a negative one
is rejected and the default used, because it would invert the clamp and pin the
camera at a fixed offset instead of widening anything.

**Smoothing is chosen per connection, and only loopback counts as local.** A tracker
sending to `127.0.0.1` gets `LocalSmoothing`; anything else, including a tracker
running on this very PC that sends to the machine's own LAN address, is classified
remote and gets `RemoteSmoothing`. If you point a phone at this PC's LAN IP, that is
the `RemoteSmoothing` path. At the default `0.15` the pose settles with a 23.5 ms
time constant, the same at any refresh rate.

`Fov=0` (default) leaves the field of view to the game, which has its own FOV slider in
Options > Graphics. Setting a value renders the scene at that FOV **at the game's default
zoom**: what the mod applies is the difference between your value and the game's default,
added to whatever FOV the game asks for, so a weapon zoom still removes the same number of
degrees it always did. The crosshair is projected with the value the scene is actually
rendered at, so it keeps marking where the shot lands. Values outside 20-170 degrees are
ignored and the game's own field of view is kept, with a line in the log saying so. The
mod changes the FOV the scene is drawn with and nothing else: weapon zooms, camera
modifiers and everything else that reads the FOV still see the game's own value.

**A zoom does not make head tracking stronger.** When the game narrows the field of view -
a scripted scene, a camera modifier - the same head turn would otherwise sweep the view
much further, because a narrower view puts more screen behind every degree. The mod scales
the head down by exactly what the zoom multiplied it by, so a head turn moves the picture
the same distance whatever the game is doing to the FOV. Leaning is scaled the same way,
for the same reason. Head roll is not, because a tilt turns the picture by its own angle at
any field of view. The scale only ever reduces: a field of view wider than the game's
default leaves the head at 1:1 rather than amplifying it. If you set `Fov`, that value
becomes the baseline the zooms are measured against, so your own choice of FOV is never
treated as a zoom.

`[Collision] Enabled=1` (default) stops a lean from pushing the camera into a wall.
Each frame the mod traces the lean it is about to apply against the world's geometry
and stops the camera `Margin` world units short of the first surface in the way,
measured along that surface. It is a hard stop, not a slowdown: keep pushing your head
forward against a wall and the view holds where it is until you move back. Only the
rendered camera is affected - the trace reads the world and changes nothing in it, and
where your shots go is unchanged either way. When whatever you were leaning against
clears, the view returns to your real head position over about a fifth of a second, so
stepping out from behind a doorframe mid-lean does not snap. Set `Enabled=0` to turn
the clamp off entirely.

`WorldSpaceYaw=true` (default) keeps yaw rotating around the world up-axis, so "up" stays gravity-aligned even when you look up or down. Set it to `false` for camera-local yaw, which follows the camera's current up-axis. Toggle it at runtime with `Page Down` or `Ctrl+Shift+H` without restarting.

## Troubleshooting

**Mod not loading**
- Confirm `dinput8.dll` and `DishonoredHeadTracking.asi` are both in `Binaries/Win32/`.
- Launch through Steam, not by running `Dishonored.exe` directly.
- Look for `HeadTracking.log` in `Binaries/Win32/`. If it is missing, the loader did not pick up the `.asi`. The log is rewritten from scratch on every launch; the previous launch is kept as `HeadTracking.prev.log`, which is the one to send if the game crashed and you have relaunched since.

**No tracking response**
- Make sure OpenTrack is running and Started, with output set to UDP on `127.0.0.1:4242`.
- Check that port `4242` is not blocked by your firewall.
- Open `HeadTracking.log` and read the heartbeat line about whether OpenTrack data is being received.
- The same log says whether tracking reached the rendered view. `Scene-view injection confirmed` means it did. A repeating `Viewpoint: N calls in the last 5s, none injected` means the mod is loaded but is not driving the camera, and that is the line to report.

**Another game or app already has port 4242**
- Only one process can hold the tracker port. If another head tracking mod (or a second copy of a game) is still running when Dishonored starts, `HeadTracking.log` records `Failed to bind UDP port 4242` and repeats `Still waiting for UDP port 4242` every 30 seconds.
- Close the other game. The mod retries the bind every 500ms on its own, so it takes the port back within about a second and logs `Bound UDP port 4242 after Ns of waiting - tracking is live`. Nothing needs restarting.

**Jittery or unstable tracking**
- Raise `LocalSmoothing` (tracker on this PC) or `RemoteSmoothing` (tracker on your phone or another network device) toward `1.0` in the INI.
- For wireless or phone trackers, increase smoothing in the tracker app as well.

**Nothing happens, and the log says "Staying dormant"**
- The mod pins its hooks to byte offsets in one specific build of the game, and refuses to touch any other one rather than crash it. Supported: the Steam retail build dated 2022-02-17. If yours differs, `HeadTracking.log` says whether it is newer or older, and the game runs vanilla.
- If the log says the exe is "tampered/repacked", the mod will not engage on a modified binary.

**The crosshair sits slightly off where the shot lands when I lean**
- Expected, and it only affects leaning, not looking. The crosshair follows the aim direction, and positional tracking moves the rendered eye away from the eye the shot leaves from, so the crosshair sits off the impact by roughly your lean divided by the distance to the target. It is largest close up and shrinks with range. Rotation is unaffected. Set `Enabled=0` under `[Position]` if you would rather have no lean at all.

**Wrong rotation axis**
- Set the matching `Invert` flag (`InvertYaw`, `InvertPitch`, or `InvertRoll`) to `1`. These ship off: the engine's own sign conventions are handled inside the mod, so you only need one of these if your tracker itself reports an axis backwards.

**Yaw feels wrong when looking up or down at extreme angles**
- Toggle between world-locked and camera-local yaw with `Page Down` or `Ctrl+Shift+H`. World-locked (default) is horizon-stable; camera-local follows the camera's current up-axis.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod's `.asi`. The Ultimate ASI Loader (`dinput8.dll`) is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

## Building from Source

```bash
git clone --recurse-submodules https://github.com/itsloopyo/dishonored-headtracking
cd dishonored-headtracking
pixi run build-release
```

Requires Visual Studio 2022 (with the C++ workload) and CMake. Output lands at `bin/Release/DishonoredHeadTracking.asi`.

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- Dishonored developed by Arkane Studios, published by Bethesda Softworks.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG (MIT).
- [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause).
- [OpenTrack](https://github.com/opentrack/opentrack) (ISC).
- Built on the shared [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core) framework.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Arkane Studios or Bethesda Softworks. Use at your own risk.
