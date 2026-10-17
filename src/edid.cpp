#include "edid.hpp"
#include <algorithm>
#include <cmath>

namespace autohdr
{
	namespace
	{
		constexpr size_t block_size = 128;

		bool checksum_ok(const uint8_t *block)
		{
			unsigned sum = 0;
			for (size_t i = 0; i < block_size; ++i)
				sum += block[i];
			return (sum & 0xFF) == 0;
		}

		double max_code_to_nits(uint8_t cv)
		{
			return cv == 0 ? 0.0 : 50.0 * std::exp2(cv / 32.0);
		}

		edid_hdr_info parse_hdr_block(const uint8_t *b, unsigned len)
		{
			edid_hdr_info info;
			info.has_hdr_block = true;
			if (len >= 2)
				info.hdr_pq = (b[2] & 0x04) != 0;
			if (len >= 4)
				info.max_nits = max_code_to_nits(b[4]);
			if (len >= 5)
				info.maxfall_nits = max_code_to_nits(b[5]);
			if (len >= 6 && info.max_nits > 0)
			{
				const double r = b[6] / 255.0;
				info.min_nits = info.max_nits * r * r / 100.0;
			}
			return info;
		}

		bool scan_cta(const uint8_t *ext, edid_hdr_info &out)
		{
			// The data block area ends before the checksum byte; clamp a corrupt DTD offset.
			const size_t d = std::min<size_t>(ext[2], block_size - 1);
			size_t k = 4;
			while (k < d)
			{
				const unsigned tag = ext[k] >> 5;
				const unsigned len = ext[k] & 0x1F;
				if (k + 1 + len > d)
					break;
				if (tag == 7 && len >= 1 && ext[k + 1] == 6)
				{
					out = parse_hdr_block(ext + k, len);
					return true;
				}
				k += 1 + len;
			}
			return false;
		}
	}

	std::optional<edid_hdr_info> parse_edid(const uint8_t *data, size_t size)
	{
		static const uint8_t header[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
		if (data == nullptr || size < block_size)
			return std::nullopt;
		if (!std::equal(header, header + 8, data) || !checksum_ok(data))
			return std::nullopt;

		const size_t present = size / block_size - 1;
		const size_t count = std::min<size_t>(data[126], present);
		for (size_t i = 1; i <= count; ++i)
		{
			const uint8_t *ext = data + i * block_size;
			if (ext[0] != 0x02 || !checksum_ok(ext))
				continue;
			edid_hdr_info info;
			if (scan_cta(ext, info))
				return info;
		}
		return edid_hdr_info{};
	}
}
