# vk-AutoHDR

A ReShade add-on (`vk-AutoHDR.addon64`) that gives SDR-only Vulkan apps on Linux HDR output, in the style of Windows AutoHDR. It uses the "proxy image" feature of the ReShade fork (branch `linux-vulkan`, PR #14 or later) to force a scRGB swap chain while the app keeps rendering in its own 8-bit format, runs the PumboAutoHDR shader on it, and calibrates the shader from what the compositor and monitor report.

Targets: native Linux Vulkan apps (emulators such as RetroArch, Dolphin, PCSX2) and Proton games (DXVK/vkd3d appear to the layer as Vulkan apps).

## Requirements

- The ReShade fork on branch `linux-vulkan` (PR #14 or later), built, with `ReShade64.so` available as a Vulkan layer.
- HDR surface formats exist only on Wayland surfaces. The app must be one of:
  - native Wayland (Qt: `QT_QPA_PLATFORM=wayland`; SDL: `SDL_VIDEODRIVER=wayland`) using its Vulkan backend;
  - Wine's Wayland driver;
  - running under gamescope with `--hdr-enabled`.

  X11/Xwayland surfaces are SDR only; the add-on steps aside there. The overlay panel and the log then name the environment variable that makes the app run on Wayland (for example `SDL_VIDEODRIVER=wayland`), chosen from the toolkit libraries the process has loaded.
- KDE HDR enabled for the target monitor.
- Build time: `libwayland-client`, `wayland-scanner` and `wayland-protocols` (>= 1.47, for `color-management-v1` version 2); `libwayland-client` >= 1.23, found through `pkg-config`.
- PumboAutoHDR at commit `915ae162c7dc134a1835355507e2bf0a3fc2bd4e`, installed as `reshade-shaders/Shaders/Pumbo/AdvancedAutoHDR.fx` (under `~/.local/share/reshade/`).
- `Deband.fx` from the standard reshade-shaders, at `reshade-shaders/Shaders/Deband.fx`.

## What installing does

- Per-window calibration and the X11 hint need the fork layer with commit b1432acf (branch `autohdr-wsi`, "WSI private data"); with an older layer both stay off and the log says so once.
- Install the `linux-vulkan` fork layer (PR #14 or later) **before** the add-on. An older `ReShade64.so` without the G1 check can fail swap chain creation on X11/Xwayland (including Proton's default).
- Once installed, the add-on is active in every game that loads the layer, unless that game's `ReShade.ini` has `[AUTOHDR] Enable=0`.
- The add-on never enables or disables techniques. It writes the Pumbo and UI uniforms and the panel tells you what to turn on: enable `Deband`, `AutoHDR_UI_Capture`, `AdvancedAutoHDR` and `AutoHDR_UI_Apply` yourself in ReShade's Home tab (or load `presets/AutoHDR.ini`), in that order (Deband first, then UI capture, then AdvancedAutoHDR, then UI apply). Disable AdvancedAutoHDR on non-engaged (SDR) swap chains. Other effects then run on linear scRGB, not sRGB.
- For Proton, set `PROTON_ENABLE_WAYLAND=1` so the Wine Wayland driver provides an HDR-capable surface.

## Build

Needs CMake >= 4.4 and GCC >= 16 (C++26). `RESHADE_SOURCE_DIR` (fork checkout, default `../reshade` next to this repo) supplies the ReShade headers; `RESHADE_LIBRARY` (default `$RESHADE_SOURCE_DIR/build-linux/ReShade64.so`) is the fork's built library, which the add-on links against. Override either with `-D`.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j16
ctest --test-dir build --output-on-failure
cmake --install build --prefix ~/.local
```

The install step writes:

- `~/.local/share/reshade/vk-AutoHDR.addon64`
- `~/.local/share/reshade/reshade-shaders/Shaders/AutoHDR/AutoHDR_UI.fx`
- `~/.local/share/reshade/presets/AutoHDR.ini`

`test_shader_names` also checks the Pumbo shader's uniform names if it is installed at the default path (override with `-DAUTOHDR_PUMBO_FX=...`); the check is skipped if the file is missing.

### Smoke test

`tools/smoke_vkcube.sh [hdr|hdroff|wayland|xcb|nopumbo|wlnocm|all]` runs end to end under the fork's layer and the built add-on, isolated through `XDG_DATA_HOME` (nothing under `~/.local` is touched). The run uses a private `XDG_RUNTIME_DIR` and drops the Wayland, X11 and D-Bus session variables, and the layer is enabled for the app only. Every app runs inside headless gamescope (`--backend headless`; engaged runs add `--hdr-enabled --hdr-debug-force-support`), so no window opens. `wayland` is an alias of `hdr`.

- Engaged runs use `vkgears`, because `vkcube` 1.4.363 picks A2B10G10R10 on HDR surfaces and the add-on correctly steps aside for 10-bit formats. The step-aside run uses `vkcube --wsi xcb`.
- The `wlnocm` run is `vkcube --wsi wayland` inside the same HDR gamescope, which offers no `wp_color_manager_v1`: it checks that the add-on notices and falls back to KWin/EDID values.
- Requires `gamescope`, `mesa-utils` (`vkgears`) and `vulkan-tools` (`vkcube`).
- Environment overrides: `FORK_SRC`, `FORK_BUILD`, `ADDON_BUILD`, `SHADER_SRC`, `ENGAGE_APP`.

## Per-game setup

1. In `~/.local/share/reshade/configurations/<game>/ReShade.ini`, set under `[GENERAL]`:
   ```
   PresetPath=/home/<you>/.local/share/reshade/presets/AutoHDR.ini
   ```
   Copy the preset per game if you want per-game tweaks.
2. Set up KDE as in [Recommended KDE setup](#recommended-kde-setup).

## Recommended KDE setup

Set the screen's **SDR brightness to 203 nits** and its **peak brightness** to the screen's real peak:

```
kscreen-doctor -o                                        # find the connector, e.g. HDMI-A-1
kscreen-doctor output.HDMI-A-1.sdr-brightness.203
```

Or System Settings > Display & Monitor > the screen > High Dynamic Range > SDR Brightness. If the EDID peak is too low (common on OLEDs and on monitors with several HDR modes), set the peak brightness override there to the mode's real peak.

Why 203:

- **Headroom.** AutoHDR can only make highlights brighter than SDR white. With SDR brightness at the screen's peak (e.g. 450/450) there is no room left. 203 nits is the HDR reference white (ITU-R BT.2408), so a 450-nit screen gets 2.2x and a 1000-nit screen about 5x.
- **Measurements read true nits.** KWin shows each app's declared reference white at your SDR brightness. NVIDIA's Wayland driver declares scRGB reference white as 203 nits. Analysis shaders (e.g. Lilium's HDR analysis) compute nits as scRGB value x 80, the Windows convention, and cannot see that remap. When SDR brightness is 203 the remap is 1:1 and their readings are physical nits. At any other value, physical = reading x SDR brightness / 203 (with Mesa: / 80).

If you prefer a brighter desktop, keep your SDR brightness: the add-on still puts game white at `GameWhiteNits` (default 203, never above SDR brightness), so highlights keep their headroom. Analysis readings are then scaled as above.

## Config keys

Section `[AUTOHDR]` in the game's `ReShade.ini`. Out-of-range values are clamped; unparsable values fall back to the default. Spaces around values are accepted, e.g. `Mask0=0.1, 0.2, 0.5, 0.6`; a leading `+` is not.

| Key | Default | Range / values | Meaning |
|---|---|---|---|
| `Enable` | `1` | 0, 1 | Master switch. Applies on the next swap chain recreation. |
| `UseManual` | `0` | 0, 1 | Use `ReferenceWhiteNits`/`PeakNits` instead of detected display values. |
| `ReferenceWhiteNits` | `200` | 10 to 10000 | SDR white level. Used when `UseManual=1`, for EDID-only detection, and when KWin's `sdrBrightness` is <= 0. |
| `PeakNits` | `600` | 10 to 10000 | Display peak, used when `UseManual=1`. |
| `Connector` | empty (auto) | connector name | Display to calibrate against, e.g. `HDMI-A-1`. |
| `GameWhiteNits` | `203` | 0, or 10 to 10000 | SDR white for the game in physical nits, capped at the screen's reference white (KDE's SDR brightness). 203 is the BT.2408 HDR reference white; 0 matches the desktop. Leaves headroom when KDE's SDR brightness sits at the peak. |
| `SdrWhiteScale` | `1` | 0.25 to 4 | Scales the game white. |
| `MaxNitsOverride` | `0` (auto) | 0 to 10000 | Pumbo max nits in physical nits. Bypasses the 3.75x clamp, not the floor (SDR white). |
| `ScrgbWhiteNits` | `0` (auto) | 0, or 10 to 10000 | scRGB nits (1.0 = 80) that KWin shows at the screen's reference white. Auto: 203 for NVIDIA's Wayland driver (it declares the Windows scRGB reference), 80 otherwise (Mesa declares none; KWin assumes 80). |
| `UiWhiteScale` | `1` | 0.25 to 4 | Brightness of masked UI regions. |
| `Mask0` .. `Mask7` | unset | `x0,y0,x1,y1` | UI mask rectangles, normalized coordinates. Out-of-range coordinates are clamped to [0,1]; corners are reordered; empty rectangles are dropped. |

The add-on's overlay edits the same keys. Slider and number-field edits are saved when the edit completes.

## Per-window calibration

On a Wayland swap chain the add-on asks the compositor which screen the game window is on (`wp_color_management_surface_feedback_v1` on the game's own `wl_surface`) and calibrates from that screen's reference white and peak. It follows the window when you move it to another screen, with no extra roundtrips and nothing blocking the render thread (events are dispatched each frame from the private queue that the game's own socket reads fill). The log shows `AutoHDR: compositor reports reference white ... for this window`.

Precedence per swap chain: manual values (`UseManual=1`) > the compositor's values for the window > KWin `kwinoutputconfig.json` > EDID > assumed defaults. `Connector=` and the overlay's connector list only affect the KWin/EDID fallback, so the overlay hides them while the compositor supplies the values. When game white is close to the screen's peak the overlay warns about low headroom.

The fallback is used when the compositor does not offer `wp_color_manager_v1` (logged once) or the surface is X11. Inside gamescope the KWin/EDID fallback is used, because gamescope does not offer the protocol to clients.

## Quality expectations

- Likely better than Windows AutoHDR: the expansion is hue-preserving and the sRGB/gamma 2.2 mismatch is corrected.
- Limits: the source is 8-bit and already tonemapped, so clipped highlights are gone, bright gradients can band (deband helps), and the HUD brightens except where masked.
- For emulators this is close to the best a generic approach can do. CRT shaders gain the most: HDR headroom restores brightness lost to masks and scanlines.
- The visible effect depends on headroom (display peak / KDE SDR brightness). Under about 1.5x there is little to gain. The overlay shows the headroom and warns below 1.5x.

## Known limitations

- Apps that create sRGB views of swap chain images through `VK_KHR_swapchain_mutable_format` break under proxy images (the proxies are plain app-format images without the mutable flag). Rare, and the add-on cannot detect it.
- Apps that create 10-bit (A2B10G10R10) or other non-8-bit swap chains are not engaged; the add-on steps aside and logs it.
- The KWin/EDID fallback has no window-to-connector mapping. With several HDR outputs it relies on a single-HDR-output rule, or on the connector you pick in the overlay; the compositor path (above) does not have this limitation.
- OpenGL is supported only through Zink.
- `Enable` takes effect on the next swap chain recreation, not immediately.

## Trade-offs and next steps

- The add-on links `ReShade64.so` rather than using the header-only API, because header-only mode includes `<Windows.h>` and does not build on Linux. It uses exported `AddonInit`/`AddonUninit` instead of `DllMain`.
- KWin's JSON config is read by a purpose-built scanner instead of a JSON library (no new dependencies).
- When a swap chain is not engaged, the add-on writes nothing and the panel warns if AdvancedAutoHDR is still enabled (HDR-input math on an sRGB buffer).
- The binder retries uniform lookups every frame while dirty (a few string compares).
- Overlay edits to sliders and number fields are saved when the edit completes, so there is no live preview during a drag.
- The G1/G2 checks and the engaged path were verified by the scripted smoke test (headless gamescope) and one on-desktop run on KDE Wayland; the validation-layer comparison is manual.
- Next steps: a DXGI backend behind the same `decide_output` boundary, and a wl_output-based connector mapping.

## License

MIT. PumboAutoHDR (MIT) and `Deband.fx` are referenced, not vendored. The ReShade headers (BSD-3-Clause) come from `RESHADE_SOURCE_DIR`.
