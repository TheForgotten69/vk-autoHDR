#include "decide.hpp"
#include <cassert>
#include <string>

using namespace autohdr;
using reshade::api::color_space;
using reshade::api::format;

int main()
{
	const format formats[] = { format::b8g8r8a8_unorm, format::b8g8r8a8_unorm_srgb, format::r8g8b8a8_unorm,
	                           format::r10g10b10a2_unorm, format::r16g16b16a16_float };
	const color_space spaces[] = { color_space::srgb, color_space::scrgb, color_space::hdr10_pq };

	for (int i = 0; i < 5; ++i)
	{
		const format f = formats[i];
		const bool srgb_capable = i < 3;
		for (const color_space cs : spaces)
			for (const bool en : { true, false })
			{
				const decision d = decide_output(f, cs, en);
				if (!en)
					assert(!d.engage && d.reason == step_aside_reason::disabled);
				else if (cs != color_space::srgb)
					assert(!d.engage && d.reason == step_aside_reason::app_is_hdr);
				else if (!srgb_capable)
					assert(!d.engage && d.reason == step_aside_reason::format_not_srgb_capable);
				else
					assert(d.engage && d.format == format::r16g16b16a16_float &&
					       d.color_space == color_space::scrgb && d.reason == step_aside_reason::none);
				if (!d.engage)
					assert(d.format == f && d.color_space == cs);
			}
	}

	assert(decide_output(format::r8g8b8a8_unorm_srgb, color_space::srgb, true).engage);
	assert(std::string(reason_text(step_aside_reason::no_hdr_surface)) == "no HDR surface (X11/Xwayland or HDR off)");
	assert(std::string(reason_text(step_aside_reason::none)) == "engaged");
	assert(std::string(reason_text(step_aside_reason::format_not_srgb_capable)) == "format not sRGB-capable");
	return 0;
}
