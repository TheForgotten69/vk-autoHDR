#include "wayland_hint.hpp"

#include <cstring>

namespace autohdr
{
	namespace
	{
		struct rule
		{
			const char *prefix;
			const char *hint;
		};

		constexpr const char *wine_hint = "PROTON_ENABLE_WAYLAND=1 (Proton) or the Wine Wayland driver";

		constexpr rule rules[] = {
			{"libQt6Gui", "QT_QPA_PLATFORM=wayland"},
			{"libQt5Gui", "QT_QPA_PLATFORM=wayland"},
			{"libSDL3", "SDL_VIDEODRIVER=wayland"},
			{"libSDL2-2.0", "SDL_VIDEODRIVER=wayland"},
			{"libglfw", "GLFW_PLATFORM=wayland"},
			{"ntdll.so", wine_hint},
			{"winex11.so", wine_hint},
			{"winevulkan.so", wine_hint},
		};

		bool has_prefix(const std::string &s, const char *prefix)
		{
			return s.compare(0, std::strlen(prefix), prefix) == 0;
		}
	}

	const char *wayland_hint(const std::vector<std::string> &loaded_library_basenames)
	{
		for (const rule &r : rules)
			for (const std::string &lib : loaded_library_basenames)
				if (has_prefix(lib, r.prefix))
					return r.hint;
		return nullptr;
	}

	std::string x11_advice(const char *hint)
	{
		if (hint)
			return std::string("X11 surface has no HDR formats; launch with ") + hint + ", or run under gamescope --hdr-enabled.";
		return "X11 surface has no HDR formats; run the app natively on Wayland, or under gamescope --hdr-enabled.";
	}
}
