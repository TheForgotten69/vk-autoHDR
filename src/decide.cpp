#include "decide.hpp"

namespace autohdr
{
	using reshade::api::color_space;
	using reshade::api::format;

	static bool is_srgb_capable(format f)
	{
		switch (f)
		{
		case format::b8g8r8a8_unorm:
		case format::b8g8r8a8_unorm_srgb:
		case format::r8g8b8a8_unorm:
		case format::r8g8b8a8_unorm_srgb:
			return true;
		default:
			return false;
		}
	}

	decision decide_output(format app_format, color_space app_color_space, bool enabled)
	{
		decision d;
		d.format = app_format;
		d.color_space = app_color_space;

		if (!enabled)
			d.reason = step_aside_reason::disabled;
		else if (app_color_space != color_space::srgb)
			d.reason = step_aside_reason::app_is_hdr;
		else if (!is_srgb_capable(app_format))
			d.reason = step_aside_reason::format_not_srgb_capable;
		else
		{
			d.engage = true;
			d.format = format::r16g16b16a16_float;
			d.color_space = color_space::scrgb;
		}
		return d;
	}

	const char *reason_text(step_aside_reason reason)
	{
		switch (reason)
		{
		case step_aside_reason::none: return "engaged";
		case step_aside_reason::disabled: return "disabled";
		case step_aside_reason::app_is_hdr: return "app is HDR";
		case step_aside_reason::format_not_srgb_capable: return "format not sRGB-capable";
		case step_aside_reason::no_hdr_surface: return "no HDR surface (X11/Xwayland or HDR off)";
		}
		return "unknown";
	}
}
