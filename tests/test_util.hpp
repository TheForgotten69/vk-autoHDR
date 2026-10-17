#pragma once

#include "display_info.hpp"

#include <cassert>
#include <cmath>
#include <string>

inline bool near(double a, double b, double eps)
{
	return std::fabs(a - b) <= eps;
}

inline std::string read_file(const std::string &path)
{
	const auto text = autohdr::read_text_file(path);
	assert(text && "cannot open file");
	return *text;
}
