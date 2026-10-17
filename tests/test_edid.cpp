#include "edid.hpp"
#include "test_util.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <string>
#include <vector>

using namespace autohdr;
using block = std::array<uint8_t, 128>;

static void fix_checksum(block &b)
{
	unsigned sum = 0;
	for (int i = 0; i < 127; ++i)
		sum += b[i];
	b[127] = static_cast<uint8_t>(-static_cast<int>(sum));
}

static void refix_base(std::vector<uint8_t> &v)
{
	block base;
	std::copy(v.begin(), v.begin() + 128, base.begin());
	fix_checksum(base);
	std::copy(base.begin(), base.end(), v.begin());
}

static block cta_ext(const std::vector<uint8_t> &blocks, bool good_checksum = true, uint8_t tag = 0x02)
{
	block b{};
	b[0] = tag;
	b[1] = 3;
	b[2] = static_cast<uint8_t>(4 + blocks.size());
	for (size_t i = 0; i < blocks.size(); ++i)
		b[4 + i] = blocks[i];
	fix_checksum(b);
	if (!good_checksum)
		b[127] = static_cast<uint8_t>(b[127] + 1);
	return b;
}

static std::vector<uint8_t> make_edid(const std::vector<block> &exts)
{
	std::vector<uint8_t> v(128 * (1 + exts.size()), 0);
	const uint8_t hdr[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
	for (int i = 0; i < 8; ++i)
		v[i] = hdr[i];
	v[126] = static_cast<uint8_t>(exts.size());
	refix_base(v);
	for (size_t i = 0; i < exts.size(); ++i)
		std::copy(exts[i].begin(), exts[i].end(), v.begin() + 128 * (i + 1));
	return v;
}

static std::vector<uint8_t> load(const char *name)
{
	const std::string bytes = read_file(std::string(AUTOHDR_TEST_DATA_DIR) + "/" + name);
	return { bytes.begin(), bytes.end() };
}

static std::optional<edid_hdr_info> parse(const std::vector<uint8_t> &v)
{
	return parse_edid(v.data(), v.size());
}

int main()
{
	{
		const auto d = load("dp3.edid");
		assert(d.size() == 384);
		const auto r = parse(d);
		assert(r && r->has_hdr_block && r->hdr_pq);
		assert(near(r->max_nits, 455.5, 0.1));
		assert(near(r->maxfall_nits, 455.5, 0.1));
		assert(near(r->min_nits, 0.494, 0.001));
	}
	{
		const auto d = load("hdmi_a_1.edid");
		assert(d.size() == 384);
		const auto r = parse(d);
		assert(r && r->has_hdr_block && r->hdr_pq);
		assert(near(r->max_nits, 426.9, 0.1));
		assert(near(r->maxfall_nits, 265.0, 0.1));
		assert(near(r->min_nits, 0.00026, 0.00001));
	}

	const std::vector<uint8_t> full_hdr = { 0xE6, 0x06, 0x05, 0x01, 102, 102, 84 };

	{
		auto v = make_edid({});
		assert(!parse_edid(v.data(), 100));
		assert(!parse_edid(nullptr, 0));
		auto bad_hdr = v;
		bad_hdr[0] = 0x01;
		assert(!parse(bad_hdr));
		auto bad_sum = v;
		bad_sum[127] = static_cast<uint8_t>(bad_sum[127] + 1);
		assert(!parse(bad_sum));
	}
	{
		const auto r = parse(make_edid({}));
		assert(r && !r->has_hdr_block);
	}
	{
		const auto r = parse(make_edid({ cta_ext(full_hdr, true, 0x70) }));
		assert(r && !r->has_hdr_block);
	}
	{
		const auto r = parse(make_edid({ cta_ext(full_hdr, false) }));
		assert(r && !r->has_hdr_block);
	}
	{
		const auto r = parse(make_edid({ cta_ext({ 0xE3, 0x06, 0x05, 0x01 }) }));
		assert(r && r->has_hdr_block && r->hdr_pq);
		assert(r->max_nits == 0 && r->maxfall_nits == 0 && r->min_nits == 0);
	}
	{
		const auto r = parse(make_edid({ cta_ext({ 0x21, 0x01, 0xE6, 0x06, 0x05, 0x01, 102, 102, 84 }) }));
		assert(r && r->has_hdr_block && r->hdr_pq);
		assert(near(r->max_nits, 455.5, 0.1));
		assert(near(r->min_nits, 0.494, 0.001));
	}
	{
		const auto r = parse(make_edid({ cta_ext({ 0xE3, 0x06, 0x08, 0x01 }) }));
		assert(r && r->has_hdr_block && !r->hdr_pq);
	}
	{
		const auto r = parse(make_edid({ cta_ext({ 0xE6, 0x06, 0x05, 0x01, 0, 0, 84 }) }));
		assert(r && r->has_hdr_block && r->max_nits == 0 && r->min_nits == 0);
	}
	{
		const auto r = parse(make_edid({ cta_ext({ 0xE9, 0x06, 0x05, 0x01, 102 }) }));
		assert(r && !r->has_hdr_block);
	}
	{ // corrupt DTD offset d=200 with a tag-7 header at b[126]; must not read past the extension
		block b{};
		b[0] = 0x02;
		b[1] = 3;
		b[2] = 200;
		b[126] = 0xE6;
		b[3] = static_cast<uint8_t>(0 - (0x02 + 3 + 200 + 0xE6 + 6));
		b[127] = 6;
		unsigned sum = 0;
		for (const uint8_t x : b)
			sum += x;
		assert((sum & 0xFF) == 0);
		const auto v = make_edid({ b });
		const auto r = parse(v);
		assert(r && !r->has_hdr_block);
	}
	{
		auto v = make_edid({ cta_ext(full_hdr) });
		v[126] = 2;
		refix_base(v);
		v.resize(256);
		const auto r = parse(v);
		assert(r && r->has_hdr_block);
		v.resize(128);
		const auto r2 = parse(v);
		assert(r2 && !r2->has_hdr_block);
	}
	return 0;
}
