# Dishonored Head Tracking

![Dishonored running with this mod](https://raw.githubusercontent.com/itsloopyo/dishonored-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Dishonored that moves the view with your head while your mouse or controller keeps aiming, driven by OpenTrack over UDP, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the rendered view; weapon fire, prompts and enemy awareness still follow your mouse or controller
- **6DOF positional tracking** - lean and peek with head position
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- Dishonored on [Steam](https://store.steampowered.com/app/205100/) (App ID 205100).
- An OpenTrack-compatible head tracker. [OpenTrack](https://github.com/opentrack/opentrack) is free and supports webcams, phones, and VR headsets.
- Windows 10 or 11. The game is 32-bit, so the mod ships as a 32-bit `.asi`.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Dishonored**, and click
**Play with head tracking**.

### Standalone Installer

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

`Page Up` / `Ctrl+Shift+G` cycles tracking mode: full tracking, then rotation only, then position only, then back to full.

`Page Down` / `Ctrl+Shift+H` switches head yaw between horizon-locked and camera-local. Horizon-locked is the default and keeps "up" where it is however the mouse is pitched.

The tracking mode and the yaw mode are saved to `CameraUnlock.ini` as soon as you change them, and come back at the next start. `End` / `Ctrl+Shift+Y` changes the current session only: whether tracking is on at startup is `EnableOnStartup`.

Each action's keys are a list in the `[Hotkeys]` section of `CameraUnlock.ini`, the chord included, so any of them can be rebound or removed.

The mod draws no text of its own, so a mode you switch to is named in `HeadTracking.log` rather than on screen.

## Configuration

Apart from creating `CameraUnlock.ini` at startup when there is none, the mod writes to it only when a hotkey changes the tracking mode or the yaw mode. It never writes `DishonoredHeadTracking.ini`, and it creates `Defaults.ini` only when there is none and never changes it. Edit `CameraUnlock.ini` with the game closed.

<!-- cameraunlock:config -->
The mod reads its settings from `Binaries\Win32\CameraUnlock.ini` in the game folder, and creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it. Changing a setting in `Defaults.ini` changes it in every game that has it set to `default`. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `WorldSpaceYaw=true`
- `RotationEnabled=true`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `PositionLimitX=0.3`
- `PositionLimitY=0.2`
- `PositionLimitYDown=0.2`
- `PositionLimitZ=0.4`
- `PositionLimitZBack=0.1`
- `CollisionEnabled=true`
- `CollisionReleaseSmoothing=0.9`
- `ToggleKey=End, Ctrl+Shift+Y`
- `CycleTrackingModeKey=PageUp, Ctrl+Shift+G`
- `YawModeKey=PageDown, Ctrl+Shift+H`

With every setting at its default, the file reads:

```ini
; Dishonored head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Change a setting in Defaults.ini to change it in
; every game that has it set to default, or write a value here instead of default to change it
; for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=default
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=default

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=default
; How far, in metres, leaning left or right can move the view.
PositionLimitX=default
; How far, in metres, raising your head can move the view.
PositionLimitY=default
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=default
; How far, in metres, leaning forward can move the view.
PositionLimitZ=default
; How far, in metres, leaning back can move the view.
PositionLimitZBack=default
; true: leaning stops at walls instead of moving the view through them.
; Only games whose mod sweeps the level for walls read this; the rest ignore it.
CollisionEnabled=default
; How far, in centimetres, the view is held off a wall when you lean into it.
CollisionMargin=20.0
; The game's own trace mask the wall check runs with. 8382 is 0x20BE: level
; geometry, movers, terrain, blocking volumes and props, but not characters, so a
; carried body does not stop the lean.
; CollisionChannel=8382
; How gently the view eases back out after a wall stopped a lean.
; 0 is the quickest, 1 the slowest.
CollisionReleaseSmoothing=default

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=default
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=default

[Camera]
; Horizontal field of view in degrees at the game's default zoom: 0, or 20 to 170.
; 0 keeps the game's own FOV - Dishonored has an FOV slider in Options > Graphics,
; so set this only to go past what that slider offers. The difference between this
; and the game's default is added to whatever FOV the game asks for, so weapon zooms
; still zoom by the same amount.
Fov=0.0
```
<!-- /cameraunlock:config -->

`Fov=0.0` (default) leaves the field of view to the game, which has its own FOV slider in
Options > Graphics. Setting a value renders the scene at that FOV **at the game's default
zoom**: what the mod applies is the difference between your value and the game's default,
added to whatever FOV the game asks for, so a weapon zoom still removes the same number of
degrees it always did. The crosshair is projected with the value the scene is actually
rendered at, so it keeps marking where the shot lands. `Fov` takes 0 or a value from 20 to
170 degrees. The mod changes the FOV the scene is drawn with and nothing else: weapon zooms,
camera modifiers and everything else that reads the FOV still see the game's own value.

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

`CollisionEnabled` (on by default) stops a lean from pushing the camera into a wall.
Each frame the mod sweeps a sphere of radius `CollisionMargin` centimetres along the lean
it is about to apply, so the edge of a doorframe or the corner of a table beside the
camera's path stops it as well as a wall straight ahead, and the camera stays that far
off every surface. Level geometry, doors and other moving parts, terrain and the props the
game's own traces hit all count; characters do not, so a body you are carrying does not
stop a lean. `CollisionChannel` is that list, as the game's own trace mask. It is a hard
stop, not a slowdown: keep pushing your head forward against a wall and the view holds
where it is until you move back. Only the rendered camera is affected - the check reads
the world and changes nothing in it, and where your shots go is unchanged either way.
When whatever you were leaning against clears, the view eases back to your real head
position, over about a fifth of a second at the default `CollisionReleaseSmoothing` of
0.9, so stepping out from behind a doorframe mid-lean does not snap.

**Smoothing is chosen per connection, and only loopback counts as local.** A tracker
sending to `127.0.0.1` gets `LocalSmoothing`; anything else, including a tracker
running on this very PC that sends to the machine's own LAN address, is classified
remote and gets `RemoteSmoothing`. If you point a phone at this PC's LAN IP, that is
the `RemoteSmoothing` path.

The game's crosshair always follows the aim point, and there is no setting for
sensitivity, inversion or deadzone: set those in your tracker.

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
- Raise `LocalSmoothing` (tracker on this PC) or `RemoteSmoothing` (tracker on your phone or another network device) toward `1.0` in `CameraUnlock.ini`.
- For wireless or phone trackers, increase smoothing in the tracker app as well.

**Nothing happens, and the log says "Staying dormant"**
- The mod pins its hooks to byte offsets in one specific build of the game, and refuses to touch any other one rather than crash it. Supported: the Steam retail build dated 2022-02-17. If yours differs, `HeadTracking.log` says whether it is newer or older, and the game runs vanilla.
- If the log says the exe is "tampered/repacked", the mod will not engage on a modified binary.

**The crosshair sits slightly off where the shot lands when I lean**
- Expected, and it only affects leaning, not looking. The crosshair follows the aim direction, and positional tracking moves the rendered eye away from the eye the shot leaves from, so the crosshair sits off the impact by roughly your lean divided by the distance to the target. It is largest close up and shrinks with range. Rotation is unaffected. Cycle to rotation only with `Page Up` / `Ctrl+Shift+G` if you would rather have no lean at all.

**Wrong rotation axis**
- Invert that axis in your tracker. The engine's own sign conventions are handled inside the mod, so this is only needed if your tracker itself reports an axis backwards.

**Yaw feels wrong when looking up or down at extreme angles**
- Toggle between world-locked and camera-local yaw with `Page Down` or `Ctrl+Shift+H`. World-locked (default) is horizon-stable; camera-local follows the camera's current up-axis.

## Updating

Download the new release and run `install.cmd` again. Your `CameraUnlock.ini` is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod's `.asi` and keeps `CameraUnlock.ini` and `DishonoredHeadTracking.ini`, so a reinstall starts with your settings. The Ultimate ASI Loader (`dinput8.dll`) is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

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
