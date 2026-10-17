#include "display_info.hpp"
#include "test_util.hpp"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

using namespace autohdr;
namespace fs = std::filesystem;

static edid_hdr_info parse(const std::string &bytes)
{
	auto r = parse_edid(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
	assert(r);
	return *r;
}

static void write_file(const fs::path &p, const std::string &s)
{
	std::ofstream f(p, std::ios::binary);
	f << s;
}

static kwin_output ko(const char *c, bool hdr, double sdr, double peak = 0)
{
	kwin_output o;
	o.connector = c;
	o.hdr = hdr;
	o.sdr_brightness = sdr;
	o.peak_override = peak;
	return o;
}

int main()
{
	const std::string dir = AUTOHDR_TEST_DATA_DIR;
	const std::string dp3 = read_file(dir + "/dp3.edid"), hdmi = read_file(dir + "/hdmi_a_1.edid");
	std::map<std::string, edid_hdr_info> edids{{"DP-3", parse(dp3)}, {"HDMI-A-1", parse(hdmi)}};
	auto kwin = parse_kwin_outputs(read_file(dir + "/kwinoutputconfig.json"));
	assert(kwin);
	autohdr_config cfg;

	assert(std::string(source_text(display_source::manual)) == "manual");
	assert(std::string(source_text(display_source::compositor)) == "compositor");
	assert(std::string(source_text(display_source::kwin)) == "KWin");
	assert(std::string(source_text(display_source::edid)) == "EDID");
	assert(std::string(source_text(display_source::assumed)) == "assumed");

	{
		autohdr_config c;
		c.use_manual = true;
		c.reference_white_nits = 250;
		c.peak_nits = 800;
		auto d = resolve_display_info(c, kwin, edids);
		assert(d.source == display_source::manual && d.reference_white_nits == 250 && d.peak_nits == 800);
		assert(!d.reference_white_assumed && !d.peak_assumed);
	}
	{
		auto d = resolve_display_info(cfg, kwin, edids);
		assert(d.source == display_source::kwin && d.connector == "DP-3");
		assert(near(d.reference_white_nits, 455, 1e-9) && near(d.peak_nits, 455.5, 0.1) && !d.peak_assumed);
		assert(!d.reference_white_assumed);
		assert((d.hdr_connectors == std::vector<std::string>{"DP-3"}));
	}
	{
		std::vector<kwin_output> v{ko("HDMI-A-1", true, 450, 450), ko("DP-3", true, 455)};
		auto d = resolve_display_info(cfg, v, edids);
		assert(d.connector == "HDMI-A-1" && d.reference_white_nits == 450 && d.peak_nits == 450);
		assert((d.hdr_connectors == std::vector<std::string>{"HDMI-A-1", "DP-3"}));
		autohdr_config c = cfg;
		c.connector = "DP-3";
		assert(resolve_display_info(c, v, edids).connector == "DP-3");
	}
	{
		std::vector<kwin_output> v{ko("DP-3", false, 200)};
		auto d = resolve_display_info(cfg, v, edids);
		assert(d.source == display_source::edid && d.connector == "DP-3");
		assert(d.reference_white_nits == 200 && d.reference_white_assumed && near(d.peak_nits, 455.5, 0.1));
		assert(!d.peak_assumed);
	}
	{
		auto d = resolve_display_info(cfg, std::nullopt, {});
		assert(d.source == display_source::assumed && d.reference_white_nits == 200 && d.peak_nits == 600);
		assert(d.reference_white_assumed && d.peak_assumed);
	}
	{
		std::vector<kwin_output> v{ko("DP-3", true, 0)};
		auto d = resolve_display_info(cfg, v, edids);
		assert(d.reference_white_nits == cfg.reference_white_nits && d.reference_white_assumed);
	}
	{
		std::vector<kwin_output> v{ko("DP-9", true, 300)};
		auto d = resolve_display_info(cfg, v, edids);
		assert(d.connector == "DP-9" && d.peak_nits == 600 && d.peak_assumed);
	}

	{
		const fs::path root = fs::temp_directory_path() / ("autohdr_drm_" + std::to_string(getpid()));
		std::error_code ec;
		fs::remove_all(root, ec);
		auto card = [&](const char *name, const char *status, const std::string &edid) {
			fs::create_directories(root / name, ec);
			write_file(root / name / "status", status);
			write_file(root / name / "edid", edid);
		};
		card("card1-DP-3", "connected\n", dp3);
		card("card1-HDMI-A-1", "connected\n", hdmi);
		card("card0-DP-4", "disconnected\n", dp3);
		card("card1-HDMI-A-2", "  connected\t ", hdmi);
		card("card1-DP-1", "connected\n", "");
		fs::create_directories(root / "card1", ec);
		fs::create_directories(root / "renderD128", ec);
		write_file(root / "version", "x");
		auto m = read_connected_edids(root);
		assert(m.size() == 3 && m.count("DP-3") && m.count("HDMI-A-1") && m.count("HDMI-A-2") && !m.count("DP-4"));
		assert(read_connected_edids(root / "missing").empty());
		fs::remove_all(root, ec);
	}

	{
		auto save = [](const char *n) { const char *v = std::getenv(n); return v ? std::optional<std::string>(v) : std::nullopt; };
		auto xdg = save("XDG_CONFIG_HOME"), home = save("HOME");
		setenv("XDG_CONFIG_HOME", "/x", 1);
		assert(kwin_config_path() == fs::path("/x/kwinoutputconfig.json"));
		unsetenv("XDG_CONFIG_HOME");
		setenv("HOME", "/h", 1);
		assert(kwin_config_path() == fs::path("/h/.config/kwinoutputconfig.json"));
		unsetenv("HOME");
		assert(kwin_config_path().empty());
		if (xdg) setenv("XDG_CONFIG_HOME", xdg->c_str(), 1);
		if (home) setenv("HOME", home->c_str(), 1);
	}

	assert(!read_text_file("/nonexistent/autohdr/file"));
	return 0;
}
