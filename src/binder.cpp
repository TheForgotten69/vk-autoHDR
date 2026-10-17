#include <imgui.h>
#include <reshade.hpp>

#include "binder.hpp"
#include "names.hpp"

#include <cstring>
#include <vector>

namespace autohdr
{
	namespace
	{
		bool same(const char *a, const char *b)
		{
			return std::strcmp(a, b) == 0;
		}

		technique_status status_of(reshade::api::effect_runtime *runtime, const char *effect, const char *technique)
		{
			technique_status st;
			const reshade::api::effect_technique handle = runtime->find_technique(effect, technique);
			if (handle.handle != 0)
			{
				st.found = true;
				st.enabled = runtime->get_technique_state(handle);
			}
			return st;
		}
	}

	bool apply_engaged(reshade::api::effect_runtime *runtime, const pumbo_values &values,
	                   const ui_mask_values &ui, bool show_outlines)
	{
		const std::vector<uniform_assignment> assignments = pumbo_assignments(values);
		std::vector<reshade::api::effect_uniform_variable> variables;
		variables.reserve(assignments.size());
		for (const uniform_assignment &a : assignments)
		{
			const reshade::api::effect_uniform_variable variable = runtime->find_uniform_variable(pumbo_effect, a.name);
			if (variable.handle == 0)
			{
				return false;
			}
			variables.push_back(variable);
		}

		for (size_t i = 0; i < assignments.size(); ++i)
		{
			switch (assignments[i].type)
			{
			case uniform_type::uint_:
				runtime->set_uniform_value_uint(variables[i], static_cast<uint32_t>(assignments[i].value));
				break;
			case uniform_type::float_:
				runtime->set_uniform_value_float(variables[i], assignments[i].value);
				break;
			case uniform_type::bool_:
				runtime->set_uniform_value_bool(variables[i], assignments[i].value != 0.f);
				break;
			}
		}
		const auto masks = runtime->find_uniform_variable(ui_effect, ui_masks_uniform);
		const auto count = runtime->find_uniform_variable(ui_effect, ui_mask_count_uniform);
		const auto scale = runtime->find_uniform_variable(ui_effect, ui_white_scale_uniform);
		const auto outlines = runtime->find_uniform_variable(ui_effect, ui_show_outlines_uniform);
		if (masks.handle != 0 && count.handle != 0 && scale.handle != 0 && outlines.handle != 0)
		{
			runtime->set_uniform_value_float(masks, ui.rects.data(), ui.rects.size());
			runtime->set_uniform_value_int(count, ui.count);
			runtime->set_uniform_value_float(scale, ui.white_scale);
			runtime->set_uniform_value_bool(outlines, show_outlines);
		}
		return true;
	}

	technique_snapshot take_technique_snapshot(reshade::api::effect_runtime *runtime)
	{
		technique_snapshot snap;
		snap.deband = status_of(runtime, deband_effect, deband_technique);
		snap.ui_capture = status_of(runtime, ui_effect, ui_capture_technique);
		snap.pumbo = status_of(runtime, pumbo_effect, pumbo_technique);
		snap.ui_apply = status_of(runtime, ui_effect, ui_apply_technique);

		if (snap.deband.found || snap.ui_capture.found || snap.pumbo.found || snap.ui_apply.found)
		{
			int index = 0;
			runtime->enumerate_techniques(nullptr, [&](reshade::api::effect_runtime *rt, reshade::api::effect_technique technique) {
				char name[128] = "", effect[256] = "";
				rt->get_technique_name(technique, name);
				rt->get_technique_effect_name(technique, effect);
				const struct { technique_status *st; const char *effect, *name; } wanted[] = {
					{ &snap.deband, deband_effect, deband_technique },
					{ &snap.ui_capture, ui_effect, ui_capture_technique },
					{ &snap.pumbo, pumbo_effect, pumbo_technique },
					{ &snap.ui_apply, ui_effect, ui_apply_technique },
				};
				for (const auto &w : wanted)
					if (w.st->found && same(effect, w.effect) && same(name, w.name))
						w.st->order = index;
				++index;
			});
		}
		return snap;
	}
}
