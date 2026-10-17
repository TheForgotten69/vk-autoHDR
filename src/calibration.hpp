#pragma once

#include "config.hpp"
#include "display_info.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace autohdr
{
	struct pumbo_values
	{
		float output_white_level_nits = 80.f;
		float auto_hdr_max_nits = 80.f;
	};

	enum class uniform_type { uint_, float_, bool_ };
	struct uniform_assignment
	{
		const char *name;
		uniform_type type;
		float value;
	};

	// scRGB nits that KWin shows at the screen's reference white: the WSI's declared reference (NVIDIA 203, Mesa none = 80).
	constexpr double scrgb_white_default = 80.0, scrgb_white_nvidia = 203.0;

	double game_white(const display_info &display, const autohdr_config &config);

	pumbo_values compute_pumbo_values(const display_info &display, const autohdr_config &config, double scrgb_white);
	std::vector<uniform_assignment> pumbo_assignments(const pumbo_values &values);
	double headroom(const display_info &display, const autohdr_config &config);
	constexpr double headroom_warning_threshold = 1.5;

	struct ui_mask_values
	{
		std::array<float, max_masks * 4> rects{};
		int32_t count = 0;
		float white_scale = 1.f;
	};
	ui_mask_values make_ui_mask_values(const autohdr_config &config);
}
