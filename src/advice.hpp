#pragma once

#include <vector>

namespace autohdr
{
	struct technique_status
	{
		bool found = false;
		bool enabled = false;
		int order = -1;
	};

	struct technique_snapshot
	{
		technique_status deband, ui_capture, pumbo, ui_apply;
	};

	enum class advice
	{
		pumbo_not_installed,
		enable_pumbo,
		pumbo_on_sdr,
		enable_ui_techniques,
		ui_techniques_without_masks,
		wrong_order,
		enable_deband,
	};

	std::vector<advice> advise(const technique_snapshot &t, bool engaged, int mask_count);
	const char *advice_text(advice a);
	bool is_warning(advice a);
}
