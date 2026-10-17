#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace autohdr {

struct kwin_output {
	std::string connector;
	std::string edid_id;
	bool hdr = false;
	double sdr_brightness = 0;
	double peak_override = 0;
};

std::optional<std::vector<kwin_output>> parse_kwin_outputs(std::string_view json);

}
