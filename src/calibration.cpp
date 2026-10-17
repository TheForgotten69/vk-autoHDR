#include "calibration.hpp"
#include "names.hpp"
#include <algorithm>

namespace autohdr
{
	static double effective_reference(const display_info &display)
	{
		return display.reference_white_nits > 0 ? display.reference_white_nits : 200.0;
	}

	double game_white(const display_info &display, const autohdr_config &config)
	{
		const double ref = effective_reference(display);
		const double white = config.game_white_nits > 0 ? std::min<double>(config.game_white_nits, ref) : ref;
		return white * config.sdr_white_scale;
	}

	pumbo_values compute_pumbo_values(const display_info &display, const autohdr_config &config, double scrgb_white)
	{
		const double ref = effective_reference(display);
		const double white = game_white(display, config);
		const double physical_max = config.max_nits_override > 0 ? config.max_nits_override : std::min(display.peak_nits, 3.75 * white);

		const double to_scrgb = scrgb_white / ref;
		pumbo_values values;
		values.output_white_level_nits = static_cast<float>(white * to_scrgb);
		values.auto_hdr_max_nits = static_cast<float>(std::max(white, physical_max) * to_scrgb);
		return values;
	}

	std::vector<uniform_assignment> pumbo_assignments(const pumbo_values &values)
	{
		return {
			{ "IN_COLOR_SPACE", uniform_type::uint_, 4.f },
			{ "SOURCE_HDR_WHITE_LEVEL_NITS", uniform_type::float_, 80.f },
			{ "FIX_SRGB_2_2_GAMMA_MISMATCH_TYPE", uniform_type::uint_, 1.f },
			{ "OUT_COLOR_SPACE", uniform_type::uint_, 1.f },
			{ "AUTO_HDR_METHOD", uniform_type::uint_, 1.f },
			{ "AUTO_HDR_SHOULDER_START_ALPHA", uniform_type::float_, 0.f },
			{ "OUTPUT_WHITE_LEVEL_NITS", uniform_type::float_, values.output_white_level_nits },
			{ "AUTO_HDR_MAX_NITS", uniform_type::float_, values.auto_hdr_max_nits },
			{ "HDR_TONEMAP", uniform_type::bool_, 0.f },
		};
	}

	double headroom(const display_info &display, const autohdr_config &config)
	{
		return display.peak_nits / game_white(display, config);
	}

	ui_mask_values make_ui_mask_values(const autohdr_config &config)
	{
		ui_mask_values out;
		out.white_scale = config.ui_white_scale;
		const size_t n = std::min(config.masks.size(), max_masks);
		out.count = static_cast<int32_t>(n);
		for (size_t i = 0; i < n; ++i)
		{
			const mask_rect &m = config.masks[i];
			out.rects[i * 4 + 0] = m.x0;
			out.rects[i * 4 + 1] = m.y0;
			out.rects[i * 4 + 2] = m.x1;
			out.rects[i * 4 + 3] = m.y1;
		}
		return out;
	}
}
