#pragma once

#include "config.hpp"

namespace autohdr
{
	class reshade_config_store final : public config_store
	{
	public:
		std::optional<std::vector<std::string>> get(const char *key) const override;
		void set(const char *key, const std::vector<std::string> &elements) override;
	};
}
