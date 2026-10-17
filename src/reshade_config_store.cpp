#include <imgui.h>
#include <reshade.hpp>

#include "reshade_config_store.hpp"

namespace autohdr
{
	namespace
	{
		constexpr const char *section = "AUTOHDR";
	}

	std::optional<std::vector<std::string>> reshade_config_store::get(const char *key) const
	{
		size_t size = 0;
		if (!reshade::get_config_value(nullptr, section, key, nullptr, &size) || size == 0)
			return std::nullopt;

		std::string buffer(size, '\0');
		if (!reshade::get_config_value(nullptr, section, key, buffer.data(), &size))
			return std::nullopt;
		buffer.resize(size);

		std::vector<std::string> elements;
		size_t begin = 0;
		while (begin < buffer.size())
		{
			const size_t end = buffer.find('\0', begin);
			if (end == std::string::npos)
			{
				elements.push_back(buffer.substr(begin));
				break;
			}
			elements.push_back(buffer.substr(begin, end - begin));
			begin = end + 1;
		}
		return elements;
	}

	void reshade_config_store::set(const char *key, const std::vector<std::string> &elements)
	{
		std::string joined;
		for (size_t i = 0; i < elements.size(); ++i)
		{
			if (i != 0)
				joined += '\0';
			joined += elements[i];
		}
		reshade::set_config_value(nullptr, section, key, joined.c_str(), joined.size());
	}
}
