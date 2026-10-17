#include "display_info.hpp"
#include "text_util.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace autohdr
{
	const char *source_text(display_source s)
	{
		switch (s)
		{
		case display_source::manual: return "manual";
		case display_source::compositor: return "compositor";
		case display_source::kwin: return "KWin";
		case display_source::edid: return "EDID";
		default: return "assumed";
		}
	}

	namespace
	{
		bool contains(const std::vector<std::string> &names, const std::string &name)
		{
			return std::find(names.begin(), names.end(), name) != names.end();
		}

		const std::string *preferred_or_first(const std::vector<std::string> &names, const std::string &preferred)
		{
			if (names.empty())
				return nullptr;
			const auto it = std::find(names.begin(), names.end(), preferred);
			return it != names.end() ? &*it : &names.front();
		}
	}

	display_info resolve_display_info(const autohdr_config &config,
	                                  const std::optional<std::vector<kwin_output>> &kwin,
	                                  const std::map<std::string, edid_hdr_info> &edids)
	{
		display_info d;

		if (config.use_manual)
		{
			d.source = display_source::manual;
			d.connector = config.connector;
			d.reference_white_nits = config.reference_white_nits;
			d.peak_nits = config.peak_nits;
			d.reference_white_assumed = d.peak_assumed = false;
			return d;
		}

		if (kwin)
		{
			for (const kwin_output &o : *kwin)
				if (o.hdr && !contains(d.hdr_connectors, o.connector))
					d.hdr_connectors.push_back(o.connector);
			if (const std::string *name = preferred_or_first(d.hdr_connectors, config.connector))
			{
				const kwin_output *chosen = nullptr;
				for (const kwin_output &o : *kwin)
					if (o.hdr && o.connector == *name)
					{
						chosen = &o;
						break;
					}
				d.source = display_source::kwin;
				d.connector = chosen->connector;
				d.reference_white_assumed = !(chosen->sdr_brightness > 0);
				d.reference_white_nits = d.reference_white_assumed ? config.reference_white_nits : chosen->sdr_brightness;
				const auto it = edids.find(chosen->connector);
				if (chosen->peak_override > 0)
				{
					d.peak_nits = chosen->peak_override;
					d.peak_assumed = false;
				}
				else if (it != edids.end() && it->second.max_nits > 0)
				{
					d.peak_nits = it->second.max_nits;
					d.peak_assumed = false;
				}
				else
				{
					d.peak_nits = 600;
					d.peak_assumed = true;
				}
				return d;
			}
		}

		for (const auto &e : edids)
			if (e.second.hdr_pq && e.second.max_nits > 0)
				d.hdr_connectors.push_back(e.first);
		if (const std::string *name = preferred_or_first(d.hdr_connectors, config.connector))
		{
			d.source = display_source::edid;
			d.connector = *name;
			d.reference_white_nits = config.reference_white_nits;
			d.reference_white_assumed = true;
			d.peak_nits = edids.at(*name).max_nits;
			d.peak_assumed = false;
		}
		return d;
	}

	fs::path kwin_config_path()
	{
		const char *xdg = std::getenv("XDG_CONFIG_HOME");
		if (xdg && *xdg)
			return fs::path(xdg) / "kwinoutputconfig.json";
		const char *home = std::getenv("HOME");
		if (home && *home)
			return fs::path(home) / ".config" / "kwinoutputconfig.json";
		return {};
	}

	std::optional<std::string> read_text_file(const fs::path &path)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f)
			return std::nullopt;
		return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	}

	static std::string connector_from_entry(const std::string &name)
	{
		if (name.compare(0, 4, "card") != 0)
			return {};
		size_t i = 4;
		while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i])))
			++i;
		if (i == 4 || i + 1 >= name.size() || name[i] != '-')
			return {};
		return name.substr(i + 1);
	}

	std::map<std::string, edid_hdr_info> read_connected_edids(const fs::path &drm_root)
	{
		std::map<std::string, edid_hdr_info> result;
		std::error_code ec;
		fs::directory_iterator it(drm_root, ec), end;
		for (; !ec && it != end; it.increment(ec))
		{
			const std::string connector = connector_from_entry(it->path().filename().string());
			if (connector.empty())
				continue;
			const auto status = read_text_file(it->path() / "status");
			if (!status)
				continue;
			if (trim(*status) != "connected")
				continue;
			const auto bytes = read_text_file(it->path() / "edid");
			if (!bytes || bytes->empty())
				continue;
			if (auto info = parse_edid(reinterpret_cast<const uint8_t *>(bytes->data()), bytes->size()))
				result[connector] = *info;
		}
		return result;
	}

	display_info detect_display_info(const autohdr_config &config)
	{
		if (config.use_manual)
			return resolve_display_info(config, std::nullopt, {});
		std::optional<std::vector<kwin_output>> kwin;
		const fs::path path = kwin_config_path();
		if (!path.empty())
			if (const auto text = read_text_file(path))
				kwin = parse_kwin_outputs(*text);
		return resolve_display_info(config, kwin, read_connected_edids("/sys/class/drm"));
	}
}
