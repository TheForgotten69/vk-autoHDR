#include "config.hpp"
#include "text_util.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>
#include <system_error>
#include <utility>

namespace autohdr
{
	bool operator==(const mask_rect &a, const mask_rect &b)
	{
		return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
	}

	bool operator==(const autohdr_config &a, const autohdr_config &b)
	{
		return a.enable == b.enable && a.use_manual == b.use_manual &&
			a.reference_white_nits == b.reference_white_nits && a.peak_nits == b.peak_nits &&
			a.connector == b.connector && a.game_white_nits == b.game_white_nits && a.sdr_white_scale == b.sdr_white_scale &&
			a.max_nits_override == b.max_nits_override && a.scrgb_white_nits == b.scrgb_white_nits &&
			a.ui_white_scale == b.ui_white_scale &&
			a.masks == b.masks;
	}

	namespace
	{
		std::optional<float> parse_float(const std::string &s)
		{
			float v = 0.f;
			const char *const end = s.data() + s.size();
			const auto r = std::from_chars(s.data(), end, v);
			if (r.ec != std::errc() || r.ptr != end || !std::isfinite(v))
				return std::nullopt;
			return v;
		}

		std::string format_float(float v)
		{
			char buf[32];
			const auto r = std::to_chars(buf, buf + sizeof(buf), v);
			return std::string(buf, r.ptr);
		}

		std::optional<std::vector<std::string>> elements(const config_store &store, const char *key)
		{
			auto v = store.get(key);
			if (v)
				for (std::string &e : *v)
					e = trim(e);
			return v;
		}

		const std::string *scalar(const std::optional<std::vector<std::string>> &v)
		{
			return (v && v->size() == 1) ? &v->front() : nullptr;
		}

		bool load_bool(const config_store &store, const char *key, bool def)
		{
			const auto v = elements(store, key);
			if (const std::string *s = scalar(v))
			{
				if (*s == "0") return false;
				if (*s == "1") return true;
			}
			return def;
		}

		float load_float(const config_store &store, const char *key, float def)
		{
			const auto v = elements(store, key);
			if (const std::string *s = scalar(v))
				if (const auto f = parse_float(*s))
					return *f;
			return def;
		}

		float sanitize_float(float v, float def, float lo, float hi)
		{
			return std::isfinite(v) ? std::clamp(v, lo, hi) : def;
		}

		std::optional<mask_rect> load_mask(const config_store &store, const char *key)
		{
			const auto v = elements(store, key);
			if (!v || v->size() != 4)
				return std::nullopt;
			float c[4];
			for (size_t i = 0; i < 4; ++i)
			{
				const auto f = parse_float((*v)[i]);
				if (!f)
					return std::nullopt;
				c[i] = *f;
			}
			return mask_rect{c[0], c[1], c[2], c[3]};
		}

		std::optional<mask_rect> sanitize_mask(const mask_rect &m)
		{
			const float v[4] = {m.x0, m.y0, m.x1, m.y1};
			float c[4];
			for (size_t i = 0; i < 4; ++i)
			{
				if (!std::isfinite(v[i]))
					return std::nullopt;
				c[i] = std::clamp(v[i], 0.f, 1.f);
			}
			const mask_rect r = {std::min(c[0], c[2]), std::min(c[1], c[3]), std::max(c[0], c[2]), std::max(c[1], c[3])};
			if (r.x0 >= r.x1 || r.y0 >= r.y1)
				return std::nullopt;
			return r;
		}

		std::string mask_key(size_t i)
		{
			return "Mask" + std::to_string(i);
		}

		void save_bool(config_store &store, const char *key, bool v)
		{
			store.set(key, {v ? "1" : "0"});
		}

		void save_float(config_store &store, const char *key, float v)
		{
			store.set(key, {format_float(v)});
		}
	}

	void sanitize(autohdr_config &c)
	{
		const autohdr_config d;
		c.reference_white_nits = sanitize_float(c.reference_white_nits, d.reference_white_nits, nits_min, nits_max);
		c.peak_nits = sanitize_float(c.peak_nits, d.peak_nits, nits_min, nits_max);
		c.game_white_nits = sanitize_float(c.game_white_nits, d.game_white_nits, 0.f, nits_max);
		if (c.game_white_nits < nits_min)
			c.game_white_nits = 0.f;
		c.sdr_white_scale = sanitize_float(c.sdr_white_scale, d.sdr_white_scale, 0.25f, 4.f);
		c.max_nits_override = sanitize_float(c.max_nits_override, d.max_nits_override, 0.f, nits_max);
		c.scrgb_white_nits = sanitize_float(c.scrgb_white_nits, d.scrgb_white_nits, 0.f, nits_max);
		if (c.scrgb_white_nits < nits_min)
			c.scrgb_white_nits = 0.f;
		c.ui_white_scale = sanitize_float(c.ui_white_scale, d.ui_white_scale, 0.25f, 4.f);
		std::vector<mask_rect> kept;
		for (const mask_rect &m : c.masks)
		{
			if (kept.size() >= max_masks)
				break;
			if (const auto r = sanitize_mask(m))
				kept.push_back(*r);
		}
		c.masks = std::move(kept);
	}

	autohdr_config load_config(const config_store &store)
	{
		const autohdr_config d;
		autohdr_config c;
		c.enable = load_bool(store, "Enable", d.enable);
		c.use_manual = load_bool(store, "UseManual", d.use_manual);
		c.reference_white_nits = load_float(store, "ReferenceWhiteNits", d.reference_white_nits);
		c.peak_nits = load_float(store, "PeakNits", d.peak_nits);
		const auto connector = elements(store, "Connector");
		if (const std::string *s = scalar(connector))
			c.connector = *s;
		c.game_white_nits = load_float(store, "GameWhiteNits", d.game_white_nits);
		c.sdr_white_scale = load_float(store, "SdrWhiteScale", d.sdr_white_scale);
		c.max_nits_override = load_float(store, "MaxNitsOverride", d.max_nits_override);
		c.scrgb_white_nits = load_float(store, "ScrgbWhiteNits", d.scrgb_white_nits);
		c.ui_white_scale = load_float(store, "UiWhiteScale", d.ui_white_scale);
		for (size_t i = 0; i < max_masks; ++i)
			if (const auto m = load_mask(store, mask_key(i).c_str()))
				c.masks.push_back(*m);
		sanitize(c);
		return c;
	}

	void save_config(const autohdr_config &config, config_store &store)
	{
		save_bool(store, "Enable", config.enable);
		save_bool(store, "UseManual", config.use_manual);
		save_float(store, "ReferenceWhiteNits", config.reference_white_nits);
		save_float(store, "PeakNits", config.peak_nits);
		store.set("Connector", config.connector.empty() ? std::vector<std::string>{} : std::vector<std::string>{config.connector});
		save_float(store, "GameWhiteNits", config.game_white_nits);
		save_float(store, "SdrWhiteScale", config.sdr_white_scale);
		save_float(store, "MaxNitsOverride", config.max_nits_override);
		save_float(store, "ScrgbWhiteNits", config.scrgb_white_nits);
		save_float(store, "UiWhiteScale", config.ui_white_scale);
		for (size_t i = 0; i < max_masks; ++i)
		{
			if (i < config.masks.size())
			{
				const mask_rect &m = config.masks[i];
				store.set(mask_key(i).c_str(), {format_float(m.x0), format_float(m.y0), format_float(m.x1), format_float(m.y1)});
			}
			else
				store.set(mask_key(i).c_str(), {});
		}
	}
}
