#include "kwin_config.hpp"
#include <cctype>
#include <charconv>

namespace autohdr {
namespace {

constexpr int max_depth = 64;

struct scalar {
	enum { other, string, number, boolean } kind = other;
	std::string str;
	double num = 0;
	bool flag = false;
};

class reader {
public:
	explicit reader(std::string_view s) : s_(s) {}

	bool parse_document(std::vector<kwin_output> &out)
	{
		scalar v;
		if (!value(0, v, out))
			return false;
		skip_ws();
		return p_ == s_.size();
	}

private:
	std::string_view s_;
	size_t p_ = 0;

	void skip_ws()
	{
		while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\t' || s_[p_] == '\n' || s_[p_] == '\r'))
			++p_;
	}
	bool eat(char c)
	{
		skip_ws();
		if (p_ < s_.size() && s_[p_] == c) {
			++p_;
			return true;
		}
		return false;
	}
	bool literal(std::string_view word)
	{
		if (s_.substr(p_, word.size()) != word)
			return false;
		p_ += word.size();
		return true;
	}

	bool string_value(std::string &out)
	{
		if (p_ >= s_.size() || s_[p_] != '"')
			return false;
		++p_;
		out.clear();
		while (p_ < s_.size()) {
			const char c = s_[p_++];
			if (c == '"')
				return true;
			if (static_cast<unsigned char>(c) < 0x20)
				return false;
			if (c != '\\') {
				out += c;
				continue;
			}
			if (p_ >= s_.size())
				return false;
			const char e = s_[p_++];
			switch (e) {
			case '"': case '\\': case '/': out += e; break;
			case 'b': out += '\b'; break;
			case 'f': out += '\f'; break;
			case 'n': out += '\n'; break;
			case 'r': out += '\r'; break;
			case 't': out += '\t'; break;
			case 'u':
				if (p_ + 4 > s_.size())
					return false;
				for (size_t i = 0; i < 4; ++i)
					if (!std::isxdigit(static_cast<unsigned char>(s_[p_ + i])))
						return false;
				out += "\\u";
				out.append(s_.substr(p_, 4));
				p_ += 4;
				break;
			default: return false;
			}
		}
		return false;
	}

	bool digits()
	{
		const size_t start = p_;
		while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9')
			++p_;
		return p_ > start;
	}

	// JSON number grammar; from_chars alone would also accept inf/nan.
	bool number_value(double &out)
	{
		const size_t start = p_;
		if (p_ < s_.size() && s_[p_] == '-')
			++p_;
		if (p_ < s_.size() && s_[p_] == '0')
			++p_;
		else if (!digits())
			return false;
		if (p_ < s_.size() && s_[p_] == '.') {
			++p_;
			if (!digits())
				return false;
		}
		if (p_ < s_.size() && (s_[p_] == 'e' || s_[p_] == 'E')) {
			++p_;
			if (p_ < s_.size() && (s_[p_] == '+' || s_[p_] == '-'))
				++p_;
			if (!digits())
				return false;
		}
		const char *b = s_.data() + start, *e = s_.data() + p_;
		const auto r = std::from_chars(b, e, out);
		return r.ec == std::errc() && r.ptr == e;
	}

	bool value(int depth, scalar &v, std::vector<kwin_output> &out)
	{
		if (depth > max_depth)
			return false;
		skip_ws();
		if (p_ >= s_.size())
			return false;
		v = scalar();
		switch (s_[p_]) {
		case '{': return object(depth, out);
		case '[': return array(depth, out);
		case '"': v.kind = scalar::string; return string_value(v.str);
		case 't': v.kind = scalar::boolean; v.flag = true; return literal("true");
		case 'f': v.kind = scalar::boolean; return literal("false");
		case 'n': return literal("null");
		default: v.kind = scalar::number; return number_value(v.num);
		}
	}

	bool array(int depth, std::vector<kwin_output> &out)
	{
		++p_;
		if (eat(']'))
			return true;
		scalar v;
		do {
			if (!value(depth + 1, v, out))
				return false;
		} while (eat(','));
		return eat(']');
	}

	bool object(int depth, std::vector<kwin_output> &out)
	{
		++p_;
		kwin_output o;
		bool has_connector = false;
		if (!eat('}')) {
			do {
				skip_ws();
				std::string key;
				scalar v;
				if (!string_value(key) || !eat(':') || !value(depth + 1, v, out))
					return false;
				if (v.kind == scalar::string && key == "connectorName") {
					o.connector = v.str;
					has_connector = true;
				} else if (v.kind == scalar::string && key == "edidIdentifier") {
					o.edid_id = v.str;
				} else if (v.kind == scalar::boolean && key == "highDynamicRange") {
					o.hdr = v.flag;
				} else if (v.kind == scalar::number && key == "sdrBrightness") {
					o.sdr_brightness = v.num;
				} else if (v.kind == scalar::number && key == "maxPeakBrightnessOverride") {
					o.peak_override = v.num;
				}
			} while (eat(','));
			if (!eat('}'))
				return false;
		}
		if (has_connector)
			out.push_back(std::move(o));
		return true;
	}
};

}

std::optional<std::vector<kwin_output>> parse_kwin_outputs(std::string_view json)
{
	std::vector<kwin_output> out;
	if (!reader(json).parse_document(out))
		return std::nullopt;
	return out;
}

}
