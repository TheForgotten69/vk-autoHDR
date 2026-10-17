#include "surface_report.hpp"

#include "config.hpp"

namespace autohdr
{
	std::optional<surface_values> resolve_surface(const surface_report &r, const char **why_invalid)
	{
		const auto fail = [&](const char *why) -> std::optional<surface_values> {
			if (why_invalid)
				*why_invalid = why;
			return std::nullopt;
		};
		if (!r.reference_lum)
			return fail("no reference luminance");
		const double ref = *r.reference_lum;
		if (!(ref >= nits_min && ref <= nits_max))
			return fail("reference luminance out of range");

		double peak;
		if (r.target_max_lum)
			peak = *r.target_max_lum;
		else if (r.target_max_cll)
			peak = static_cast<double>(*r.target_max_cll);
		else if (r.max_lum)
			peak = *r.max_lum;
		else
			return fail("no peak luminance");
		if (!(peak >= ref))
			return fail("peak luminance below reference luminance");
		if (!(peak <= nits_max))
			return fail("peak luminance out of range");

		return surface_values{ ref, peak, peak <= ref };
	}
}
