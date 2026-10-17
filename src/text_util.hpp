#pragma once

#include <string>

namespace autohdr
{
	inline std::string trim(const std::string &s)
	{
		const size_t b = s.find_first_not_of(" \t\r\n");
		if (b == std::string::npos)
			return {};
		return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
	}
}
