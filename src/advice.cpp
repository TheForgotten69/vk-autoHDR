#include "advice.hpp"

namespace autohdr
{
	namespace
	{
		bool active(const technique_status &s) { return s.found && s.enabled && s.order >= 0; }
	}

	std::vector<advice> advise(const technique_snapshot &t, bool engaged, int mask_count)
	{
		std::vector<advice> out;
		const bool pumbo_on = t.pumbo.found && t.pumbo.enabled;

		if (engaged && !t.pumbo.found)
			out.push_back(advice::pumbo_not_installed);
		if (engaged && t.pumbo.found && !t.pumbo.enabled)
			out.push_back(advice::enable_pumbo);
		if (!engaged && pumbo_on)
			out.push_back(advice::pumbo_on_sdr);

		const bool capture_on = t.ui_capture.found && t.ui_capture.enabled;
		const bool apply_on = t.ui_apply.found && t.ui_apply.enabled;
		if (engaged && mask_count > 0 && !(capture_on && apply_on))
			out.push_back(advice::enable_ui_techniques);
		if (mask_count <= 0 && (capture_on || apply_on))
			out.push_back(advice::ui_techniques_without_masks);

		bool ordered = true;
		int last = -1;
		for (const technique_status *s : { &t.ui_capture, &t.pumbo, &t.ui_apply })
		{
			if (!active(*s))
				continue;
			if (s->order <= last)
				ordered = false;
			last = s->order;
		}
		if (active(t.deband))
			for (const technique_status *s : { &t.ui_capture, &t.pumbo })
				if (active(*s) && t.deband.order >= s->order)
					ordered = false;
		if (!ordered)
			out.push_back(advice::wrong_order);

		if (engaged && pumbo_on && t.deband.found && !t.deband.enabled)
			out.push_back(advice::enable_deband);
		return out;
	}

	const char *advice_text(advice a)
	{
		switch (a)
		{
		case advice::pumbo_not_installed:
			return "Install PumboAutoHDR (AdvancedAutoHDR.fx, commit 915ae16); it was not found.";
		case advice::enable_pumbo:
			return "Enable AdvancedAutoHDR in the Home tab (or load presets/AutoHDR.ini).";
		case advice::pumbo_on_sdr:
			return "Disable AdvancedAutoHDR in the Home tab: this swap chain is SDR and the image will look wrong.";
		case advice::enable_ui_techniques:
			return "Enable AutoHDR_UI_Capture and AutoHDR_UI_Apply in the Home tab (or load presets/AutoHDR.ini) so the masks take effect.";
		case advice::ui_techniques_without_masks:
			return "AutoHDR_UI_Capture/AutoHDR_UI_Apply are enabled but no masks are defined; they do nothing. Add a mask or disable them.";
		case advice::wrong_order:
			return "Reorder techniques in the Home tab: Deband, AutoHDR_UI_Capture, AdvancedAutoHDR, AutoHDR_UI_Apply (or load presets/AutoHDR.ini).";
		case advice::enable_deband:
			return "Optional: enable Deband in the Home tab to reduce banding.";
		}
		return "";
	}

	bool is_warning(advice a)
	{
		return a == advice::pumbo_on_sdr || a == advice::wrong_order ||
		       a == advice::pumbo_not_installed || a == advice::enable_pumbo;
	}
}
