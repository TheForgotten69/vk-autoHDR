#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace autohdr
{
	struct edid_hdr_info
	{
		bool has_hdr_block = false;
		bool hdr_pq = false;
		double max_nits = 0;
		double maxfall_nits = 0;
		double min_nits = 0;
	};

	std::optional<edid_hdr_info> parse_edid(const uint8_t *data, size_t size);
}
