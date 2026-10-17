#pragma once

#include <cstdint>
#include <optional>

namespace autohdr
{
	struct surface_report
	{
		std::optional<uint32_t> tf_named;
		std::optional<double> max_lum, reference_lum;
		std::optional<double> target_max_lum;
		std::optional<uint32_t> target_max_cll;
	};

	struct surface_values
	{
		double reference_white_nits;
		double peak_nits;
		bool sdr_output; // peak <= reference; not the TF, KWin reports gamma 2.2 on HDR screens too
	};

	std::optional<surface_values> resolve_surface(const surface_report &r, const char **why_invalid);
}
