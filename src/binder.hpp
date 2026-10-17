#pragma once

#include "advice.hpp"
#include "calibration.hpp"

namespace reshade::api { struct effect_runtime; }

namespace autohdr
{
	bool apply_engaged(reshade::api::effect_runtime *runtime, const pumbo_values &values,
	                   const ui_mask_values &ui, bool show_outlines);

	technique_snapshot take_technique_snapshot(reshade::api::effect_runtime *runtime);
}
