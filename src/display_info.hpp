#pragma once

#include "config.hpp"
#include "edid.hpp"
#include "kwin_config.hpp"
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace autohdr
{
	enum class display_source { manual, compositor, kwin, edid, assumed };
	const char *source_text(display_source s);

	struct display_info
	{
		display_source source = display_source::assumed;
		std::string connector;
		double reference_white_nits = 200;
		double peak_nits = 600;
		bool reference_white_assumed = true;
		bool peak_assumed = true;
		std::vector<std::string> hdr_connectors;
	};

	display_info resolve_display_info(const autohdr_config &config,
	                                  const std::optional<std::vector<kwin_output>> &kwin,
	                                  const std::map<std::string, edid_hdr_info> &edids);

	std::filesystem::path kwin_config_path();
	std::optional<std::string> read_text_file(const std::filesystem::path &path);
	std::map<std::string, edid_hdr_info> read_connected_edids(const std::filesystem::path &drm_root);
	display_info detect_display_info(const autohdr_config &config);
}
