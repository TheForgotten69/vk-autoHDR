#pragma once

#include <reshade_api_format.hpp>

namespace autohdr
{
	enum class step_aside_reason { none, disabled, app_is_hdr, format_not_srgb_capable, no_hdr_surface };

	struct decision
	{
		bool engage = false;
		reshade::api::format format = reshade::api::format::unknown;
		reshade::api::color_space color_space = reshade::api::color_space::unknown;
		step_aside_reason reason = step_aside_reason::none;
	};

	decision decide_output(reshade::api::format app_format, reshade::api::color_space app_color_space, bool enabled);
	const char *reason_text(step_aside_reason reason);
}
