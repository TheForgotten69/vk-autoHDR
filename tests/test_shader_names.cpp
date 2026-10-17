#include "calibration.hpp"
#include "names.hpp"
#include "test_util.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using namespace autohdr;

static const char *hlsl(uniform_type t)
{
	switch (t)
	{
	case uniform_type::uint_: return "uint";
	case uniform_type::float_: return "float";
	case uniform_type::bool_: return "bool";
	}
	return "";
}

static void expect(const std::string &text, const std::string &needle, const char *what)
{
	if (text.find(needle) == std::string::npos)
	{
		std::fprintf(stderr, "%s: missing \"%s\"\n", what, needle.c_str());
		assert(false);
	}
}

int main()
{
	const std::string ui = read_file(AUTOHDR_UI_FX);
	const auto assignments = pumbo_assignments({});
	assert(!assignments.empty());

	if (!autohdr::read_text_file(AUTOHDR_PUMBO_FX))
	{
		std::puts("skipping Pumbo check");
	}
	else
	{
		const std::string pumbo = read_file(AUTOHDR_PUMBO_FX);
		for (const auto &a : assignments)
			expect(pumbo, std::string("uniform ") + hlsl(a.type) + " " + a.name, "pumbo");
		expect(pumbo, std::string("technique ") + pumbo_technique, "pumbo");
	}

	expect(ui, std::string("uniform float4 ") + ui_masks_uniform + "[8]", "ui");
	expect(ui, std::string("uniform int ") + ui_mask_count_uniform, "ui");
	expect(ui, std::string("uniform float ") + ui_white_scale_uniform, "ui");
	expect(ui, std::string("uniform bool ") + ui_show_outlines_uniform, "ui");
	expect(ui, std::string("technique ") + ui_capture_technique, "ui");
	expect(ui, std::string("technique ") + ui_apply_technique, "ui");

	const std::string preset = read_file(AUTOHDR_PRESET);
	for (const auto &a : assignments)
		expect(preset, std::string("\n") + a.name + "=", "preset");
	return 0;
}
