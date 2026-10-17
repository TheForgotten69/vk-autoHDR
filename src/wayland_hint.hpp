#pragma once

#include <string>
#include <vector>

namespace autohdr
{
	const char *wayland_hint(const std::vector<std::string> &loaded_library_basenames);

	std::string x11_advice(const char *hint);
}
