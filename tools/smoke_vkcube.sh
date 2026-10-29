#!/usr/bin/env bash
# Usage: tools/smoke_vkcube.sh [hdr|hdroff|wayland|xcb|nopumbo|wlnocm|all]   (wayland = alias of hdr)
set -eu

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(dirname "$SCRIPT_DIR")
FORK_SRC=${FORK_SRC:-$REPO/../reshade}   # fork checkout, branch linux-vulkan
FORK_BUILD=${FORK_BUILD:-$FORK_SRC/build-linux}
ADDON_BUILD=${ADDON_BUILD:-$REPO/build}
SHADER_SRC=${SHADER_SRC:-$HOME/.local/share/reshade/reshade-shaders/Shaders}
# vkcube picks a 10-bit format on the HDR Wayland surface, so the engaged runs
# use an app that creates an 8-bit swapchain there.
ENGAGE_APP=${ENGAGE_APP:-vkgears}
RUN_TIMEOUT=${RUN_TIMEOUT:-20}
PROFILE=autohdrsmoke

abspath() { (cd "$1" 2>/dev/null && pwd); }
FORK_BUILD=$(abspath "$FORK_BUILD") || { echo "FAIL: FORK_BUILD not found" >&2; exit 1; }
ADDON_BUILD=$(abspath "$ADDON_BUILD") || { echo "FAIL: ADDON_BUILD not found" >&2; exit 1; }

FAILURES=0
MISSING=""   # first failed assertion of the current run

first_failure() { MISSING="${MISSING:-$1}"; }

expect() { if ! grep -qF -- "$1" "$LOG"; then first_failure "missing: $1"; fi; }
forbid() { if grep -qF -- "$1" "$LOG"; then first_failure "unexpected: $1"; fi; }

setup() { # $1 = with Pumbo (1/0), $2 = optional preset variant
	ROOT=$(mktemp -d)
	mkdir -m 0700 "$ROOT/xdg"
	local data=$ROOT/data shaders=$ROOT/data/reshade/reshade-shaders/Shaders
	local cfg=$data/reshade/configurations/$PROFILE
	mkdir -p "$data/vulkan/implicit_layer.d" "$shaders" "$cfg" "$ROOT/cache"
	sed "s|\"library_path\": *\"[^\"]*\"|\"library_path\": \"$FORK_BUILD/ReShade64.so\"|" \
		"$FORK_BUILD/ReShade64.json" > "$data/vulkan/implicit_layer.d/ReShade64.json"
	if ! grep -qF "\"$FORK_BUILD/ReShade64.so\"" "$data/vulkan/implicit_layer.d/ReShade64.json"; then
		echo "FAIL: layer JSON library_path was not rewritten to $FORK_BUILD/ReShade64.so" >&2
		exit 1
	fi
	cp "$ADDON_BUILD/vk-AutoHDR.addon64" "$data/reshade/vk-AutoHDR.addon64"
	local f
	for f in ReShade.fxh ReShadeUI.fxh Deband.fx; do ln -s "$SHADER_SRC/$f" "$shaders/$f"; done
	# ReShade's recursive search does not descend into a symlinked directory,
	# so Pumbo is a real directory of per-file symlinks.
	if [ "$1" = 1 ]; then
		mkdir "$shaders/Pumbo"
		for f in "$SHADER_SRC"/Pumbo/*; do ln -s "$f" "$shaders/Pumbo/"; done
	fi
	ln -s "$REPO/shaders/AutoHDR_UI.fx" "$shaders/AutoHDR_UI.fx"
	cat > "$cfg/ReShade.ini" <<INI
[GENERAL]
EffectSearchPaths=$shaders/**
TextureSearchPaths=$shaders/**
IntermediateCachePath=$ROOT/cache
PresetPath=$ROOT/AutoHDR.ini

[AUTOHDR]
Enable=1
GameWhiteNits=0
SdrWhiteScale=1.5
INI
	cp "$REPO/presets/AutoHDR.ini" "$ROOT/AutoHDR.ini"
	if [ "${2:-}" = pumbo_off ]; then
		sed -i '/^Techniques=/s|AdvancedAutoHDR@AdvancedAutoHDR.fx,\?||; /^Techniques=/s|,$||' "$ROOT/AutoHDR.ini"
	fi
}

run_one() { # $1 = run name, $2 = with Pumbo (1/0), $3 = optional preset variant
	local name=$1
	MISSING=""
	setup "$2" "${3:-}"
	echo "[$name] ROOT=$ROOT"
	# Headless gamescope with a private XDG_RUNTIME_DIR: nothing can reach the user's session.
	local gs_args=(--backend headless -w 1280 -h 720) app=("$ENGAGE_APP")
	if [ "$name" = xcb ]; then
		app=(vkcube --wsi xcb --c 600)
	elif [ "$name" = wlnocm ]; then
		app=(env WAYLAND_DISPLAY=gamescope-0 vkcube --wsi wayland --c 300)
		gs_args+=(--hdr-enabled --hdr-debug-force-support)
	else
		gs_args+=(--hdr-enabled --hdr-debug-force-support)
	fi
	# gamescope segfaults at teardown (rc 139): expected, rc is not evidence.
	(
		env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET -u DISPLAY -u DBUS_SESSION_BUS_ADDRESS XDG_RUNTIME_DIR=$ROOT/xdg \
			timeout -k 5 "$RUN_TIMEOUT" gamescope "${gs_args[@]}" -- \
			env XDG_DATA_HOME=$ROOT/data RESHADE_ENABLE=1 RESHADE_PROFILE=$PROFILE "${app[@]}" >"$ROOT/app.out" 2>&1 || true
	) 2>/dev/null
	if ! grep -qF "Creating headless backend" "$ROOT/app.out"; then
		echo "FAIL $name: gamescope headless backend did not start (see $ROOT/app.out)"
		FAILURES=$((FAILURES + 1))
		return
	fi

	local logs
	logs=$(find "$ROOT/data/reshade/logs" -type f 2>/dev/null || true)
	LOG=$logs
	if [ -z "$logs" ] || [ "$(printf '%s\n' "$logs" | wc -l)" -ne 1 ]; then
		echo "FAIL $name: no ReShade log (or more than one) under $ROOT/data/reshade/logs/ (layer or add-on did not load; see $ROOT/app.out)"
		FAILURES=$((FAILURES + 1))
		return
	fi

	case $name in
	hdr)
		expect "Applying add-on swap chain override: format 97, color space 1000104002."
		expect "Using Vulkan proxy swapchain images"
		expect "AutoHDR: override confirmed, swap chain is scRGB."
		expect "AutoHDR: applied uniforms ("
		expect "OUTPUT_WHITE_LEVEL_NITS=120.0"
		if ! grep -F "AutoHDR_UI.fx" "$LOG" | grep -qF "Successfully compiled"; then
			first_failure "missing: Successfully compiled (AutoHDR_UI.fx)"
		fi
		forbid "Failed to compile"
		forbid "AutoHDR: internal error"
		;;
	hdroff)
		expect "AutoHDR: override confirmed"
		expect "AutoHDR: applied uniforms ("
		expect "AutoHDR: swap chain is scRGB but AdvancedAutoHDR is disabled; enable it in the Home tab (or load presets/AutoHDR.ini)."
		forbid "AutoHDR: AdvancedAutoHDR is enabled"
		forbid "AdvancedAutoHDR.fx uniforms not found"
		forbid "AutoHDR: internal error"
		;;
	xcb)
		expect "is not supported by the surface; reverting"
		expect "AutoHDR: override did not take: no HDR surface"
		expect "AutoHDR: X11 surface has no HDR formats; run the app natively on Wayland, or under gamescope --hdr-enabled."
		expect "AutoHDR: AdvancedAutoHDR is enabled but this swap chain is SDR; disable it in the Home tab."
		forbid "Using Vulkan proxy swapchain images"
		forbid "vkCreateSwapchainKHR failed"
		;;
	wlnocm)
		expect "Vulkan WSI surface: kind=wayland"
		expect "AutoHDR: compositor does not offer wp_color_manager_v1; using KWin/EDID values."
		forbid "AutoHDR: compositor reports"
		forbid "AutoHDR: internal error"
		;;
	nopumbo)
		expect "AutoHDR: override confirmed"
		local count
		count=$(grep -cF -- "AdvancedAutoHDR.fx uniforms not found" "$LOG" || true)
		if [ "$count" != 1 ]; then
			first_failure "'uniforms not found' count is $count, expected 1"
		fi
		;;
	esac

	if [ -z "$MISSING" ]; then
		echo "PASS $name"
	else
		echo "FAIL $name: $MISSING (log: $LOG)"
		FAILURES=$((FAILURES + 1))
	fi
}

for bin in gamescope vkcube "$ENGAGE_APP"; do
	if ! command -v "$bin" >/dev/null 2>&1; then echo "FAIL: required binary not found: $bin" >&2; exit 1; fi
done
for f in "$FORK_BUILD/ReShade64.so" "$FORK_BUILD/ReShade64.json" "$ADDON_BUILD/vk-AutoHDR.addon64"; do
	if [ ! -f "$f" ]; then echo "FAIL: missing $f" >&2; exit 1; fi
done

want=${1:-all}
case $want in
all) run_one hdr 1; run_one hdroff 1 pumbo_off; run_one xcb 1; run_one nopumbo 0; run_one wlnocm 1 ;;
hdr | wayland) run_one hdr 1 ;;
hdroff) run_one hdroff 1 pumbo_off ;;
xcb) run_one xcb 1 ;;
nopumbo) run_one nopumbo 0 ;;
wlnocm) run_one wlnocm 1 ;;
*) echo "usage: $0 [hdr|hdroff|wayland|xcb|nopumbo|wlnocm|all]" >&2; exit 2 ;;
esac

[ "$FAILURES" -eq 0 ]
