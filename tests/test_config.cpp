#include "config.hpp"
#include "test_util.hpp"

#include <cassert>
#include <limits>
#include <map>

using namespace autohdr;

namespace
{
	struct map_store : config_store
	{
		std::map<std::string, std::vector<std::string>> data;

		std::optional<std::vector<std::string>> get(const char *key) const override
		{
			const auto it = data.find(key);
			if (it == data.end())
				return std::nullopt;
			return it->second;
		}
		void set(const char *key, const std::vector<std::string> &elements) override
		{
			if (elements.empty())
				data.erase(key);
			else
				data[key] = elements;
		}
	};

	bool rect_is(const mask_rect &r, float x0, float y0, float x1, float y1)
	{
		return near(r.x0, x0, 1e-6) && near(r.y0, y0, 1e-6) && near(r.x1, x1, 1e-6) && near(r.y1, y1, 1e-6);
	}
}

static void test_defaults()
{
	map_store s;
	assert(load_config(s) == autohdr_config{});
}

static void test_round_trip()
{
	autohdr_config cfg;
	cfg.enable = false;
	cfg.use_manual = true;
	cfg.reference_white_nits = 123.4f;
	cfg.peak_nits = 1000.5f;
	cfg.connector = "DP-2";
	cfg.sdr_white_scale = 1.25f;
	cfg.max_nits_override = 450.f;
	cfg.scrgb_white_nits = 203.f;
	cfg.game_white_nits = 180.f;
	cfg.ui_white_scale = 0.75f;
	cfg.masks = {{0.1f, 0.2f, 0.5f, 0.6f}, {0.f, 0.f, 1.f, 0.1f}, {0.333333f, 0.25f, 0.9f, 0.875f}};

	map_store s;
	save_config(cfg, s);
	assert(load_config(s) == cfg);
}

static void test_masks()
{
	map_store s;
	s.data["Mask0"] = {"0.1", "0.2", "0.5", "0.6"};
	s.data["Mask1"] = {"0.5", "0.6", "0.1", "0.2"};
	s.data["Mask2"] = {"-0.5", "0", "2", "1"};
	s.data["Mask3"] = {"0.1", "0.2", "0.3"};
	s.data["Mask4"] = {"a", "0", "1", "1"};
	s.data["Mask5"] = {"0.2", "0.2", "0.2", "0.5"};
	s.data["Mask7"] = {"0", "0", "0.5", "0.5"};
	s.data["Mask8"] = {"0", "0", "1", "1"};

	const autohdr_config cfg = load_config(s);
	assert(cfg.masks.size() == 4);
	assert(rect_is(cfg.masks[0], 0.1f, 0.2f, 0.5f, 0.6f));
	assert(rect_is(cfg.masks[1], 0.1f, 0.2f, 0.5f, 0.6f));
	assert(rect_is(cfg.masks[2], 0.f, 0.f, 1.f, 1.f));
	assert(rect_is(cfg.masks[3], 0.f, 0.f, 0.5f, 0.5f));
}

static void test_clamps()
{
	const auto load_one = [](const char *key, const char *value) {
		map_store s;
		s.data[key] = {value};
		return load_config(s);
	};

	assert(near(load_one("ReferenceWhiteNits", "-5").reference_white_nits, 10.f, 1e-6));
	assert(near(load_one("PeakNits", "99999").peak_nits, 10000.f, 1e-6));
	assert(near(load_one("SdrWhiteScale", "9").sdr_white_scale, 4.f, 1e-6));
	assert(near(load_one("UiWhiteScale", "0").ui_white_scale, 0.25f, 1e-6));
	assert(near(load_one("MaxNitsOverride", "-1").max_nits_override, 0.f, 1e-6));
	assert(near(load_one("ScrgbWhiteNits", "5").scrgb_white_nits, 0.f, 1e-6));
	assert(near(load_one("GameWhiteNits", "0").game_white_nits, 0.f, 1e-6));
	assert(near(load_one("GameWhiteNits", "abc").game_white_nits, 203.f, 1e-6));
	assert(near(load_one("ScrgbWhiteNits", "203").scrgb_white_nits, 203.f, 1e-6));
	assert(near(load_one("ReferenceWhiteNits", "abc").reference_white_nits, 200.f, 1e-6));
	assert(near(load_one("ReferenceWhiteNits", "250x").reference_white_nits, 200.f, 1e-6));
	assert(load_one("Enable", "yes").enable == true);
}

static void test_shrink_clears_stale()
{
	map_store s;
	s.data["Mask0"] = {"0", "0", "0.1", "0.1"};
	s.data["Mask1"] = {"0", "0", "0.2", "0.2"};
	s.data["Mask2"] = {"0", "0", "0.3", "0.3"};

	autohdr_config cfg;
	cfg.masks = {{0.1f, 0.1f, 0.4f, 0.4f}};
	save_config(cfg, s);
	assert(load_config(s).masks.size() == 1);
	assert(!s.data.count("Mask1") && !s.data.count("Mask2"));
}

static void test_connector()
{
	map_store s;
	s.data["Connector"] = {"HDMI-A-1"};
	assert(load_config(s).connector == "HDMI-A-1");
	s.data["Connector"] = {" \tDP-2 "};
	assert(load_config(s).connector == "DP-2");
}

static void test_whitespace_trim()
{
	map_store s;
	s.data["Mask0"] = {"0.1", " 0.2", "0.5 ", "\t0.6"};
	s.data["Mask1"] = {"0.1", "+0.2", "0.5", "0.6"};
	s.data["Mask2"] = {"0.1", "0.2 x", "0.5", "0.6"};
	s.data["PeakNits"] = {" 800\t"};
	s.data["Enable"] = {" 0 "};
	s.data["DebandEnable"] = {"1 "};
	s.data["UiWhiteScale"] = {"+2"};
	const autohdr_config cfg = load_config(s);
	assert(cfg.masks.size() == 1);
	assert(rect_is(cfg.masks[0], 0.1f, 0.2f, 0.5f, 0.6f));
	assert(near(cfg.peak_nits, 800.f, 1e-6));
	assert(cfg.enable == false);
	assert(near(cfg.ui_white_scale, 1.f, 1e-6));
}

static void test_sanitize()
{
	autohdr_config c;
	c.reference_white_nits = -5.f;
	c.peak_nits = 99999.f;
	c.sdr_white_scale = 9.f;
	c.ui_white_scale = 0.f;
	c.max_nits_override = -1.f;
	c.masks = {{0.5f, 0.6f, 0.1f, 0.2f}, {-0.5f, 0.f, 2.f, 1.f}, {0.2f, 0.2f, 0.2f, 0.5f}, {0.f, 0.f, 0.5f, 0.5f}};
	sanitize(c);
	assert(near(c.reference_white_nits, 10.f, 1e-6));
	assert(near(c.peak_nits, 10000.f, 1e-6));
	assert(near(c.sdr_white_scale, 4.f, 1e-6));
	assert(near(c.ui_white_scale, 0.25f, 1e-6));
	assert(near(c.max_nits_override, 0.f, 1e-6));
	assert(c.masks.size() == 3);
	assert(rect_is(c.masks[0], 0.1f, 0.2f, 0.5f, 0.6f));
	assert(rect_is(c.masks[1], 0.f, 0.f, 1.f, 1.f));
	assert(rect_is(c.masks[2], 0.f, 0.f, 0.5f, 0.5f));

	autohdr_config many;
	for (size_t i = 0; i < max_masks + 3; ++i)
		many.masks.push_back({0.f, 0.f, 0.5f, 0.5f});
	sanitize(many);
	assert(many.masks.size() == max_masks);

	autohdr_config nan;
	nan.peak_nits = std::numeric_limits<float>::quiet_NaN();
	nan.masks = {{std::numeric_limits<float>::quiet_NaN(), 0.f, 1.f, 1.f}};
	sanitize(nan);
	assert(near(nan.peak_nits, autohdr_config{}.peak_nits, 1e-6));
	assert(nan.masks.empty());

	autohdr_config ok;
	ok.peak_nits = 1000.f;
	ok.masks = {{0.1f, 0.1f, 0.4f, 0.4f}};
	const autohdr_config before = ok;
	sanitize(ok);
	assert(ok == before);
}

int main()
{
	test_defaults();
	test_round_trip();
	test_masks();
	test_clamps();
	test_shrink_clears_stale();
	test_connector();
	test_whitespace_trim();
	test_sanitize();
	return 0;
}
