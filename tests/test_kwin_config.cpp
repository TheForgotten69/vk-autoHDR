#include "kwin_config.hpp"
#include "test_util.hpp"
#include <cassert>
#include <string>

using namespace autohdr;

static std::string one_output(const char *connector, bool hdr)
{
	return std::string(R"([{"data":[{"connectorName":")") + connector + R"(","highDynamicRange":)" +
	       (hdr ? "true" : "false") + R"(,"sdrBrightness":200}],"name":"outputs"}])";
}

int main()
{
	{
		auto r = parse_kwin_outputs(read_file(std::string(AUTOHDR_TEST_DATA_DIR) + "/kwinoutputconfig.json"));
		assert(r && r->size() == 3);
		const auto &o = *r;
		assert(o[0].connector == "HDMI-A-1" && o[0].edid_id == "DEL 41546 0 52 2023 0");
		assert(!o[0].hdr && near(o[0].sdr_brightness, 450, 1e-9) && near(o[0].peak_override, 450, 1e-9));
		assert(o[1].connector == "DP-3" && o[1].edid_id == "GSM 23639 0 11 2023 0");
		assert(o[1].hdr && near(o[1].sdr_brightness, 455, 1e-9) && o[1].peak_override == 0);
		assert(o[2].connector == "DP-3" && o[2].edid_id == "NVD 0 0 0 0 0");
		assert(!o[2].hdr && near(o[2].sdr_brightness, 200, 1e-9) && o[2].peak_override == 0);
	}

	assert(!parse_kwin_outputs(""));
	assert(!parse_kwin_outputs("[{"));
	assert(!parse_kwin_outputs("[1,]"));
	assert(!parse_kwin_outputs("[] x"));
	assert(!parse_kwin_outputs("[nul]"));
	assert(!parse_kwin_outputs("[+1]"));
	assert(!parse_kwin_outputs("[nan]"));
	assert(!parse_kwin_outputs("[0x10]"));
	assert(!parse_kwin_outputs(R"({"a" 1})"));
	assert(!parse_kwin_outputs(R"(["\q"])"));
	{
		auto r = parse_kwin_outputs("[]");
		assert(r && r->empty());
	}

	{
		auto r = parse_kwin_outputs(one_output("DP-1", false));
		assert(r && r->size() == 1 && !(*r)[0].hdr);
		auto two = parse_kwin_outputs(R"([{"data":[{"connectorName":"DP-1","highDynamicRange":true},)"
		                              R"({"connectorName":"HDMI-A-1","highDynamicRange":true}],"name":"outputs"}])");
		assert(two && two->size() == 2 && (*two)[0].hdr && (*two)[1].hdr);
	}

	{
		auto r = parse_kwin_outputs(R"({"connectorName":"A\"B","mode":{"cvt":{"x":1}},"curve":[[0,-1.5e2]],"maxPeakBrightnessOverride":null})");
		assert(r && r->size() == 1);
		assert((*r)[0].connector == "A\"B" && (*r)[0].peak_override == 0 && !(*r)[0].hdr);
		auto e = parse_kwin_outputs(R"([{"connectorName":"X","sdrBrightness":2.5E2}])");
		assert(e && near((*e)[0].sdr_brightness, 250, 1e-9));
	}

	assert(!parse_kwin_outputs(std::string(100, '[')));
	assert(!parse_kwin_outputs(std::string(100, '[') + std::string(100, ']')));
	return 0;
}
