#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace autohdr
{
	struct mask_rect
	{
		float x0, y0, x1, y1;
	};

	constexpr size_t max_masks = 8;
	constexpr float nits_min = 10.f, nits_max = 10000.f;

	struct autohdr_config
	{
		bool enable = true;
		bool use_manual = false;
		float reference_white_nits = 200.f;
		float peak_nits = 600.f;
		std::string connector;
		float game_white_nits = 203.f;      // capped at the screen's reference white; 0 = follow it
		float sdr_white_scale = 1.f;
		float max_nits_override = 0.f;
		float scrgb_white_nits = 0.f;
		float ui_white_scale = 1.f;
		std::vector<mask_rect> masks;
	};

	bool operator==(const mask_rect &a, const mask_rect &b);
	bool operator==(const autohdr_config &a, const autohdr_config &b);

	struct config_store
	{
		virtual ~config_store() = default;
		virtual std::optional<std::vector<std::string>> get(const char *key) const = 0;
		virtual void set(const char *key, const std::vector<std::string> &elements) = 0;
	};

	void sanitize(autohdr_config &config);
	autohdr_config load_config(const config_store &store);
	void save_config(const autohdr_config &config, config_store &store);
}
