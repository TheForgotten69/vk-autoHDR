#pragma once

#include "calibration.hpp"
#include "config.hpp"
#include "decide.hpp"
#include "display_info.hpp"
#include "surface_report.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace reshade::api { struct effect_runtime; }

namespace autohdr { class surface_watcher; }

namespace autohdr
{
	struct runtime_binding
	{
		uint64_t applied_epoch = 0;
		bool logged = false;
		unsigned logged_advice = 0;
	};

	struct swapchain_entry
	{
		std::optional<decision> status;
		uint64_t kind = 0;
		void *surface = nullptr;
		double scrgb_white = scrgb_white_default;
	};

	bool is_x11_stepped_aside(const swapchain_entry &e);

	struct watcher_entry
	{
		std::shared_ptr<surface_watcher> watcher;
		int refs = 0;
		std::optional<surface_values> values;
		bool warned = false;
		uint64_t serial = 0;
	};
	constexpr size_t max_watcher_entries = 16;

	struct addon_state
	{
		std::mutex mutex;
		uint64_t epoch = 1;
		autohdr_config config;
		display_info display;
		ui_mask_values ui;
		bool show_outlines = false;
		std::unordered_map<uint64_t, swapchain_entry> swapchains;
		std::string x11_advice_line;
		std::unordered_map<reshade::api::effect_runtime *, runtime_binding> bindings;
		std::unordered_map<void *, watcher_entry> watchers;
		uint64_t next_watcher_serial = 1;
	};

	// Call WITH state().mutex held.
	struct effective_display
	{
		display_info info;
		double scrgb_white = scrgb_white_default;
	};
	effective_display effective_display_locked(const addon_state &s, uint64_t native);

	addon_state &state();

	// Call WITHOUT state().mutex held.
	void commit_config(const autohdr_config &next, bool redetect);

	// Call WITHOUT state().mutex held.
	void set_show_outlines(bool show);

	void draw_overlay(reshade::api::effect_runtime *runtime);
}
