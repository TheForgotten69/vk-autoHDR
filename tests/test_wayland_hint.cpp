#include "wayland_hint.hpp"
#include <cassert>
#include <cstring>

using namespace autohdr;

static bool is(const std::vector<std::string> &libs, const char *expect)
{
	const char *h = wayland_hint(libs);
	return h && std::strcmp(h, expect) == 0;
}

int main()
{
	assert(is({"libc.so.6", "libQt6Gui.so.6"}, "QT_QPA_PLATFORM=wayland"));
	assert(is({"libQt5Gui.so.5"}, "QT_QPA_PLATFORM=wayland"));
	assert(is({"libSDL3.so.0"}, "SDL_VIDEODRIVER=wayland"));
	assert(is({"libSDL2-2.0.so.0"}, "SDL_VIDEODRIVER=wayland"));
	assert(is({"libglfw.so.3"}, "GLFW_PLATFORM=wayland"));
	assert(is({"ntdll.so"}, "PROTON_ENABLE_WAYLAND=1 (Proton) or the Wine Wayland driver"));
	assert(is({"winex11.so"}, "PROTON_ENABLE_WAYLAND=1 (Proton) or the Wine Wayland driver"));
	assert(is({"winevulkan.so"}, "PROTON_ENABLE_WAYLAND=1 (Proton) or the Wine Wayland driver"));
	assert(is({"libglfw.so.3", "libQt6Gui.so.6"}, "QT_QPA_PLATFORM=wayland"));
	assert(is({"libglfw.so.3", "libSDL2-2.0.so.0"}, "SDL_VIDEODRIVER=wayland"));
	assert(wayland_hint({}) == nullptr);
	assert(wayland_hint({"libc.so.6", "libQt6Core.so.6", "libSDL2_image.so"}) == nullptr);

	assert(x11_advice("SDL_VIDEODRIVER=wayland")
		== "X11 surface has no HDR formats; launch with SDL_VIDEODRIVER=wayland, or run under gamescope --hdr-enabled.");
	assert(x11_advice(nullptr)
		== "X11 surface has no HDR formats; run the app natively on Wayland, or under gamescope --hdr-enabled.");
	return 0;
}
