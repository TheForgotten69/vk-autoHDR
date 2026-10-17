#include "calibration.hpp"
#include "names.hpp"
#include "test_util.hpp"
#include <cassert>
#include <cstring>

using namespace autohdr;

static display_info disp(double ref, double peak)
{
	display_info d;
	d.reference_white_nits = ref;
	d.peak_nits = peak;
	return d;
}

static float max_nits(double ref, double peak, float override_nits = 0.f)
{
	autohdr_config c;
	c.game_white_nits = 0.f;
	c.max_nits_override = override_nits;
	return compute_pumbo_values(disp(ref, peak), c, scrgb_white_default).auto_hdr_max_nits;
}

int main()
{
	autohdr_config cfg;
	cfg.game_white_nits = 0.f;
	{
		const pumbo_values v = compute_pumbo_values(disp(200, 455.5), cfg, scrgb_white_default);
		assert(near(v.output_white_level_nits, 80, 1e-4));
		assert(near(v.auto_hdr_max_nits, 182.2, 0.05));
	}
	assert(near(max_nits(200, 1000), 300, 1e-3));
	assert(near(max_nits(100, 300), 240, 1e-3));
	assert(near(max_nits(455, 455.5), 80.088, 0.001));
	assert(near(max_nits(450, 400), 80, 1e-4));
	{
		autohdr_config c;
		c.game_white_nits = 0.f;
		c.sdr_white_scale = 1.5f;
		assert(near(compute_pumbo_values(disp(200, 455.5), c, scrgb_white_default).output_white_level_nits, 120, 1e-4));
	}
	assert(near(max_nits(200, 455.5, 600.f), 240, 1e-3));
	assert(near(max_nits(200, 455.5, 100.f), 80, 1e-4));
	assert(near(max_nits(0, 600), 240, 1e-3));
	assert(near(max_nits(-5, 600), 240, 1e-3));
	{
		const pumbo_values v = compute_pumbo_values(disp(317, 450), cfg, scrgb_white_nvidia);
		assert(near(v.output_white_level_nits, 203, 1e-4));
		assert(near(v.auto_hdr_max_nits, 450.0 * 203 / 317, 0.01));
		assert(near(compute_pumbo_values(disp(450, 450), cfg, scrgb_white_nvidia).auto_hdr_max_nits, 203, 1e-4));
	}
	assert(near(headroom(disp(0, 600), cfg), 3.0, 1e-9));
	assert(near(headroom(disp(200, 455.5), cfg), 2.2775, 1e-4));

	{
		const autohdr_config d;
		assert(near(game_white(disp(450, 450), d), 203, 1e-9));
		assert(near(headroom(disp(450, 450), d), 450.0 / 203, 1e-9));
		const pumbo_values v = compute_pumbo_values(disp(450, 450), d, scrgb_white_nvidia);
		assert(near(v.output_white_level_nits, 203.0 * 203 / 450, 0.01));
		assert(near(v.auto_hdr_max_nits, 203, 0.01));
		const pumbo_values m = compute_pumbo_values(disp(450, 450), d, scrgb_white_default);
		assert(near(m.output_white_level_nits, 203.0 * 80 / 450, 0.01));
		assert(near(m.auto_hdr_max_nits, 80, 0.01));
		assert(near(game_white(disp(150, 600), d), 150, 1e-9));
	}

	{
		pumbo_values v;
		v.output_white_level_nits = 120.f;
		v.auto_hdr_max_nits = 240.f;
		const auto a = pumbo_assignments(v);
		assert(a.size() == 9);
		struct exp { const char *n; uniform_type t; float v; };
		const exp e[9] = {
			{ "IN_COLOR_SPACE", uniform_type::uint_, 4 },
			{ "SOURCE_HDR_WHITE_LEVEL_NITS", uniform_type::float_, 80 },
			{ "FIX_SRGB_2_2_GAMMA_MISMATCH_TYPE", uniform_type::uint_, 1 },
			{ "OUT_COLOR_SPACE", uniform_type::uint_, 1 },
			{ "AUTO_HDR_METHOD", uniform_type::uint_, 1 },
			{ "AUTO_HDR_SHOULDER_START_ALPHA", uniform_type::float_, 0 },
			{ "OUTPUT_WHITE_LEVEL_NITS", uniform_type::float_, 120 },
			{ "AUTO_HDR_MAX_NITS", uniform_type::float_, 240 },
			{ "HDR_TONEMAP", uniform_type::bool_, 0 },
		};
		for (size_t i = 0; i < 9; ++i)
		{
			assert(std::strcmp(a[i].name, e[i].n) == 0);
			assert(a[i].type == e[i].t);
			assert(a[i].value == e[i].v);
		}
	}

	{
		autohdr_config c;
		c.ui_white_scale = 1.25f;
		c.masks = { { 0.1f, 0.2f, 0.3f, 0.4f }, { 0.5f, 0.6f, 0.7f, 0.8f } };
		const ui_mask_values m = make_ui_mask_values(c);
		assert(m.count == 2);
		const float want[8] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f };
		for (int i = 0; i < 8; ++i) assert(m.rects[i] == want[i]);
		for (int i = 8; i < 32; ++i) assert(m.rects[i] == 0.f);
		assert(m.white_scale == 1.25f);
	}
	{
		autohdr_config c;
		for (int i = 0; i < 10; ++i) c.masks.push_back({ 0.f, 0.f, 0.5f, 0.5f });
		const ui_mask_values m = make_ui_mask_values(c);
		assert(m.count == 8);
		assert(m.rects[31] == 0.5f);
	}
	assert(make_ui_mask_values(autohdr_config{}).count == 0);

	assert(std::strcmp(ui_masks_uniform, "AutoHDR_UiMasks") == 0);
	return 0;
}
