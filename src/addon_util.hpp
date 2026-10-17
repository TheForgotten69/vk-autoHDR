#pragma once

#include <reshade.hpp>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <exception>
#include <locale.h>
#include <type_traits>

namespace autohdr
{
	__attribute__((format(printf, 2, 0)))
	inline void vlog_line(reshade::log::level level, const char *fmt, va_list args)
	{
		char buf[512];
		// "C" numeric locale: a decimal comma in the log would mislead (RPCS3 under fr_FR logged 455,0).
		static const locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", static_cast<locale_t>(nullptr));
		const locale_t previous = c_locale ? uselocale(c_locale) : static_cast<locale_t>(nullptr);
		std::vsnprintf(buf, sizeof(buf), fmt, args);
		if (c_locale)
			uselocale(previous);
		reshade::log::message(level, buf);
	}

	__attribute__((format(printf, 2, 3)))
	inline void log_line(reshade::log::level level, const char *fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		vlog_line(level, fmt, args);
		va_end(args);
	}

	__attribute__((format(printf, 3, 4)))
	inline void log_once(std::atomic<bool> &logged, reshade::log::level level, const char *fmt, ...)
	{
		if (logged.exchange(true))
			return;
		va_list args;
		va_start(args, fmt);
		vlog_line(level, fmt, args);
		va_end(args);
	}

	template <typename F>
	bool guarded(F &&body)
	{
		try
		{
			if constexpr (std::is_void_v<std::invoke_result_t<F &>>)
			{
				body();
				return true;
			}
			else
				return body();
		}
		catch (const std::exception &e)
		{
			log_line(reshade::log::level::error, "AutoHDR: internal error: %s", e.what());
		}
		catch (...)
		{
			log_line(reshade::log::level::error, "AutoHDR: internal error: %s", "unknown exception");
		}
		return false;
	}
}
