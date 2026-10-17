#include <imgui.h>
#include <reshade.hpp>

#include "addon_state.hpp"
#include "addon_util.hpp"
#include "binder.hpp"
#include "reshade_config_store.hpp"
#include "surface_watcher.hpp"
#include "wayland_hint.hpp"
#include <reshade_api_wsi.hpp>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <cstring>
#include <dlfcn.h>
#include <link.h>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace autohdr
{
	namespace
	{
		thread_local std::optional<decision> pending_decision;

		constexpr unsigned advice_bit(advice a) { return 1u << static_cast<unsigned>(a); }

		const watcher_entry *find_watcher_locked(const addon_state &s, uint64_t native)
		{
			const auto sc = s.swapchains.find(native);
			if (sc == s.swapchains.end() || !sc->second.surface)
				return nullptr;
			const auto w = s.watchers.find(sc->second.surface);
			return w == s.watchers.end() ? nullptr : &w->second;
		}

		watcher_entry *find_watcher_locked(addon_state &s, uint64_t native)
		{
			return const_cast<watcher_entry *>(find_watcher_locked(std::as_const(s), native));
		}

		void mark_all_dirty_locked(addon_state &s)
		{
			++s.epoch;
		}

		// NVIDIA's proprietary Wayland WSI declares scRGB reference white 203 nits (set_luminances(0, 80, 203));
		// Mesa, NVK included, declares none. Other WSIs (X11, gamescope) do not go through KWin's mapping.
		double detect_scrgb_white(reshade::api::swapchain *sc, reshade::api::wsi::surface_kind kind)
		{
			if (kind != reshade::api::wsi::surface_kind::wayland)
				return scrgb_white_default;
			reshade::api::device *dev = sc->get_device();
			uint32_t vendor = 0;
			char description[256] = "";
			if (!dev->get_property(reshade::api::device_properties::vendor_id, &vendor) || vendor != 0x10de)
				return scrgb_white_default;
			dev->get_property(reshade::api::device_properties::description, description);
			return std::strstr(description, "NVK") ? scrgb_white_default : scrgb_white_nvidia;
		}

		void recompute_locked(addon_state &s)
		{
			s.ui = make_ui_mask_values(s.config);
		}

		void log_display(const display_info &d)
		{
			log_line(reshade::log::level::info, "AutoHDR: display %s, connector '%s', reference white %.1f nits, peak %.1f nits.",
			    source_text(d.source), d.connector.c_str(), d.reference_white_nits, d.peak_nits);
		}

		void apply_surface_result(void *surface, const std::optional<surface_values> &v, const char *why)
		{
			guarded([&] {
				bool log_warning = false;
				{
					std::lock_guard<std::mutex> lock(state().mutex);
					const auto it = state().watchers.find(surface);
					if (it == state().watchers.end())
						return;
					it->second.values = v;
					if (!v && !it->second.warned)
						log_warning = it->second.warned = true;
					++state().epoch;
				}
				if (v)
					log_line(reshade::log::level::info, "AutoHDR: compositor reports reference white %.1f nits, peak %.1f nits for this window%s.",
					    v->reference_white_nits, v->peak_nits, v->sdr_output ? " (no headroom: SDR white is at the peak)" : "");
				else if (log_warning && why)
					log_line(reshade::log::level::warning, "AutoHDR: compositor values rejected (%s); using KWin/EDID values.", why);
				else if (log_warning)
					log_line(reshade::log::level::warning, "AutoHDR: compositor could not describe this window's screen; using KWin/EDID values.");
			});
		}

		surface_watcher::callbacks make_watcher_callbacks(void *surface)
		{
			surface_watcher::callbacks cb;
			cb.on_report = [surface](const surface_report &r) {
				const char *why = "unknown";
				apply_surface_result(surface, resolve_surface(r, &why), why);
			};
			cb.on_unavailable = [] {
				static std::atomic<bool> logged{false};
				log_once(logged, reshade::log::level::info, "AutoHDR: compositor does not offer wp_color_manager_v1; using KWin/EDID values.");
			};
			cb.on_failed = [surface] { apply_surface_result(surface, std::nullopt, nullptr); };
			return cb;
		}

		// Leaked watchers keep protocol objects whose wl_interface tables live in this image; ReShade's dlclose after
		// AddonUninit would make the next event crash the game's Wayland reader. Pin the module.
		void pin_module_once()
		{
			static std::once_flag once;
			std::call_once(once, [] {
				static const int anchor = 0;
				Dl_info info;
				if (dladdr(&anchor, &info) && info.dli_fname && dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE))
					return;
				log_line(reshade::log::level::warning, "AutoHDR: could not pin the add-on module; unloading it with a live Wayland window may be unsafe.");
			});
		}

		void evict_idle_watchers_locked(addon_state &s)
		{
			while (s.watchers.size() > max_watcher_entries)
			{
				auto oldest = s.watchers.end();
				for (auto it = s.watchers.begin(); it != s.watchers.end(); ++it)
					if (it->second.refs <= 0 && (oldest == s.watchers.end() || it->second.serial < oldest->second.serial))
						oldest = it;
				if (oldest == s.watchers.end())
					return;
				s.watchers.erase(oldest);
			}
		}

		void attach_watcher(uint64_t key, void *surface)
		{
			{
				std::lock_guard<std::mutex> lock(state().mutex);
				swapchain_entry &sc = state().swapchains[key];
				if (sc.surface)
					return;
				const auto it = state().watchers.find(surface);
				if (it != state().watchers.end() && it->second.watcher)
				{
					++it->second.refs;
					sc.surface = surface;
					return;
				}
			}
			pin_module_once();
			std::shared_ptr<surface_watcher> fresh = surface_watcher::create(surface, make_watcher_callbacks(surface));
			std::shared_ptr<surface_watcher> spare;
			std::lock_guard<std::mutex> lock(state().mutex);
			const auto [it, inserted] = state().watchers.try_emplace(surface);
			if (inserted)
				it->second.serial = state().next_watcher_serial++;
			if (it->second.watcher)
				spare = std::move(fresh);
			else
				it->second.watcher = std::move(fresh);
			++it->second.refs;
			state().swapchains[key].surface = surface;
			if (inserted)
				evict_idle_watchers_locked(state());
		}

		bool on_create_swapchain(reshade::api::device_api api, reshade::api::swapchain_desc &desc, void *)
		{
			return guarded([&]() -> bool {
				if (api != reshade::api::device_api::vulkan)
					return false;

				bool enable;
				{
					std::lock_guard<std::mutex> lock(state().mutex);
					enable = state().config.enable;
				}
				const decision d = decide_output(desc.back_buffer.texture.format, desc.color_space, enable);
				pending_decision = d;
				if (d.engage)
				{
					log_line(reshade::log::level::info, "AutoHDR: engaging scRGB output (application format %u, color space %u).",
					    static_cast<unsigned>(desc.back_buffer.texture.format), static_cast<unsigned>(desc.color_space));
					desc.back_buffer.texture.format = d.format;
					desc.color_space = d.color_space;
					return true;
				}
				log_line(reshade::log::level::info, "AutoHDR: stepping aside: %s.", reason_text(d.reason));
				return false;
			});
		}

		int collect_library(dl_phdr_info *info, size_t, void *data)
		{
			const char *name = info->dlpi_name;
			if (name && *name)
			{
				const char *slash = std::strrchr(name, '/');
				static_cast<std::vector<std::string> *>(data)->emplace_back(slash ? slash + 1 : name);
			}
			return 0;
		}

		std::vector<std::string> loaded_library_basenames()
		{
			std::vector<std::string> libs;
			dl_iterate_phdr(collect_library, &libs);
			return libs;
		}

		void on_init_swapchain(reshade::api::swapchain *sc, bool)
		{
			guarded([&] {
				const std::optional<decision> pending = pending_decision;
				pending_decision.reset(); // a later unrelated swap chain on this thread must not inherit it

				const uint64_t key = sc->get_native();
				std::optional<decision> status = pending;
				if (pending && pending->engage)
				{
					if (sc->get_color_space() == reshade::api::color_space::scrgb)
					{
						log_line(reshade::log::level::info, "AutoHDR: override confirmed, swap chain is scRGB.");
					}
					else
					{
						status->reason = step_aside_reason::no_hdr_surface;
						status->engage = false;
						log_line(reshade::log::level::warning, "AutoHDR: override did not take: no HDR surface (X11/Xwayland or HDR off).");
					}
				}

				uint64_t raw = 0;
				sc->get_private_data(reshade::api::wsi::surface_kind_guid, &raw);
				const auto kind = static_cast<reshade::api::wsi::surface_kind>(raw);
				swapchain_entry entry;
				entry.status = status;
				entry.kind = raw;
				const bool x11 = is_x11_stepped_aside(entry);
				if (kind == reshade::api::wsi::surface_kind::unknown && sc->get_device()->get_api() == reshade::api::device_api::vulkan)
				{
					static std::atomic<bool> logged{false};
					log_once(logged, reshade::log::level::warning, "AutoHDR: the ReShade layer does not report the window type (older layer); per-window calibration and X11 hints are off.");
				}
				std::string advice_line;
				if (x11)
				{
					static std::atomic<bool> logged{false};
					advice_line = x11_advice(wayland_hint(loaded_library_basenames()));
					log_once(logged, reshade::log::level::warning, "AutoHDR: %s", advice_line.c_str());
				}

				if (kind == reshade::api::wsi::surface_kind::wayland && sc->get_hwnd())
				{
					bool enable;
					{
						std::lock_guard<std::mutex> lock(state().mutex);
						enable = state().config.enable;
					}
					if (enable)
						attach_watcher(key, reinterpret_cast<void *>(sc->get_hwnd()));
				}

				const double scrgb_white = detect_scrgb_white(sc, kind);
				if (status && status->engage && scrgb_white != scrgb_white_default)
				{
					static std::atomic<bool> logged{false};
					log_once(logged, reshade::log::level::info, "AutoHDR: NVIDIA Wayland driver: scRGB reference white is %.0f nits (ScrgbWhiteNits overrides).", scrgb_white);
				}

				std::lock_guard<std::mutex> lock(state().mutex);
				swapchain_entry &stored = state().swapchains[key];
				stored.status = status;
				stored.kind = raw;
				stored.scrgb_white = scrgb_white;
				if (x11)
					state().x11_advice_line = advice_line;
				mark_all_dirty_locked(state());
			});
		}

		void on_destroy_swapchain(reshade::api::swapchain *sc, bool)
		{
			guarded([&] {
				const uint64_t key = sc->get_native();
				std::shared_ptr<surface_watcher> dead;
				std::lock_guard<std::mutex> lock(state().mutex);
				if (watcher_entry *w = find_watcher_locked(state(), key); w && --w->refs <= 0)
					dead = std::move(w->watcher);
				state().swapchains.erase(key);
			});
		}

		void on_init_effect_runtime(reshade::api::effect_runtime *rt)
		{
			guarded([&] {
				std::lock_guard<std::mutex> lock(state().mutex);
				state().bindings[rt] = runtime_binding{};
				++state().epoch; // a re-inserted binding must not match an epoch captured by an apply in flight
			});
		}

		void on_destroy_effect_runtime(reshade::api::effect_runtime *rt)
		{
			guarded([&] {
				std::lock_guard<std::mutex> lock(state().mutex);
				state().bindings.erase(rt);
			});
		}

		void on_reloaded_effects(reshade::api::effect_runtime *rt)
		{
			guarded([&] {
				std::lock_guard<std::mutex> lock(state().mutex);
				// Do not reset `logged`: this fires before and after loading, so resetting would log twice.
				state().bindings[rt];
				mark_all_dirty_locked(state());
			});
		}

		void on_begin_effects(reshade::api::effect_runtime *rt, reshade::api::command_list *, reshade::api::resource_view, reshade::api::resource_view)
		{
			guarded([&] {
				std::shared_ptr<surface_watcher> watcher;
				{
					std::lock_guard<std::mutex> lock(state().mutex);
					if (const watcher_entry *w = find_watcher_locked(std::as_const(state()), rt->get_native()))
						watcher = w->watcher;
				}
				if (watcher)
				{
					watcher->poll();
					watcher.reset();
				}

				bool engaged;
				pumbo_values values;
				ui_mask_values ui;
				bool outlines;
				uint64_t epoch;
				{
					std::lock_guard<std::mutex> lock(state().mutex);
					const auto it = state().bindings.find(rt);
					if (it == state().bindings.end() || it->second.applied_epoch == state().epoch)
						return;
					const auto sc = state().swapchains.find(rt->get_native());
					engaged = sc != state().swapchains.end() && sc->second.status && sc->second.status->engage;
					epoch = state().epoch;
					const effective_display eff = effective_display_locked(state(), rt->get_native());
					values = compute_pumbo_values(eff.info, state().config, eff.scrgb_white);
					ui = state().ui;
					ui.white_scale *= static_cast<float>(eff.scrgb_white / scrgb_white_default);
					outlines = state().show_outlines;
				}

				bool done = true;
				if (engaged)
					done = apply_engaged(rt, values, ui, outlines);

				unsigned advice_mask = 0;
				if (done)
					for (const advice a : advise(take_technique_snapshot(rt), engaged, ui.count))
						advice_mask |= advice_bit(a);

				bool log_missing = false;
				unsigned new_advice = 0;
				{
					std::lock_guard<std::mutex> lock(state().mutex);
					const auto it = state().bindings.find(rt);
					if (it == state().bindings.end())
						return;
					runtime_binding &b = it->second;
					if (state().epoch != epoch)
						return;
					if (done)
					{
						b.applied_epoch = epoch;
						b.logged = false;
						new_advice = advice_mask & ~b.logged_advice;
						b.logged_advice = advice_mask;
					}
					else if (!b.logged)
					{
						b.logged = true;
						log_missing = true;
					}
				}

				if (done && engaged)
					log_line(reshade::log::level::info, "AutoHDR: applied uniforms (OUTPUT_WHITE_LEVEL_NITS=%.1f, AUTO_HDR_MAX_NITS=%.1f).",
					    values.output_white_level_nits, values.auto_hdr_max_nits);
				else if (log_missing)
					log_line(reshade::log::level::info, "AutoHDR: AdvancedAutoHDR.fx uniforms not found (effects still loading, or PumboAutoHDR not installed); retrying.");

				if (new_advice & advice_bit(advice::pumbo_on_sdr))
					log_line(reshade::log::level::warning, "AutoHDR: AdvancedAutoHDR is enabled but this swap chain is SDR; disable it in the Home tab.");
				if (new_advice & advice_bit(advice::enable_pumbo))
					log_line(reshade::log::level::info, "AutoHDR: swap chain is scRGB but AdvancedAutoHDR is disabled; enable it in the Home tab (or load presets/AutoHDR.ini).");
			});
		}
	}

	addon_state &state()
	{
		static addon_state instance;
		return instance;
	}

	effective_display effective_display_locked(const addon_state &s, uint64_t native)
	{
		effective_display e;
		e.info = s.display;
		if (s.config.scrgb_white_nits > 0)
			e.scrgb_white = s.config.scrgb_white_nits;
		else if (const auto sc = s.swapchains.find(native); sc != s.swapchains.end())
			e.scrgb_white = sc->second.scrgb_white;
		if (s.config.use_manual)
			return e;
		const watcher_entry *w = find_watcher_locked(s, native);
		if (!w || !w->values)
			return e;
		const surface_values &v = *w->values;
		e.info = display_info{};
		e.info.source = display_source::compositor;
		e.info.reference_white_nits = v.reference_white_nits;
		e.info.peak_nits = v.peak_nits;
		e.info.reference_white_assumed = e.info.peak_assumed = false;
		return e;
	}

	bool is_x11_stepped_aside(const swapchain_entry &e)
	{
		using reshade::api::wsi::surface_kind;
		const auto kind = static_cast<surface_kind>(e.kind);
		return e.status && e.status->reason == step_aside_reason::no_hdr_surface && (kind == surface_kind::xcb || kind == surface_kind::xlib);
	}

	void commit_config(const autohdr_config &requested, bool redetect)
	{
		guarded([&] {
			autohdr_config next = requested;
			sanitize(next);
			reshade_config_store store;
			save_config(next, store);

			std::optional<display_info> display;
			if (redetect)
				display = detect_display_info(next);

			std::lock_guard<std::mutex> lock(state().mutex);
			state().config = next;
			if (display)
				state().display = *display;
			recompute_locked(state());
			mark_all_dirty_locked(state());
			if (display)
				log_display(state().display);
		});
	}

	void set_show_outlines(bool show)
	{
		guarded([&] {
			std::lock_guard<std::mutex> lock(state().mutex);
			state().show_outlines = show;
			mark_all_dirty_locked(state());
		});
	}
}

extern "C"
{
	__attribute__((visibility("default"))) const char *NAME = "vk-AutoHDR";
	__attribute__((visibility("default"))) const char *DESCRIPTION = "AutoHDR for SDR Vulkan apps on Linux: scRGB output calibrated to the window's screen.";
}

extern "C" __attribute__((visibility("default"))) bool AddonInit(void *addon_module, void *)
{
	using namespace autohdr;
	if (!reshade::register_addon(addon_module))
		return false;

	const bool ok = guarded([&] {
		const reshade_config_store store;
		const autohdr_config config = load_config(store);
		const display_info display = detect_display_info(config);
		{
			std::lock_guard<std::mutex> lock(state().mutex);
			state().config = config;
			state().display = display;
			recompute_locked(state());
		}
		log_display(display);
	});
	if (!ok)
	{
		reshade::unregister_addon(addon_module);
		return false;
	}

	reshade::register_event<reshade::addon_event::create_swapchain>(on_create_swapchain);
	reshade::register_event<reshade::addon_event::init_swapchain>(on_init_swapchain);
	reshade::register_event<reshade::addon_event::destroy_swapchain>(on_destroy_swapchain);
	reshade::register_event<reshade::addon_event::init_effect_runtime>(on_init_effect_runtime);
	reshade::register_event<reshade::addon_event::destroy_effect_runtime>(on_destroy_effect_runtime);
	reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(on_reloaded_effects);
	reshade::register_event<reshade::addon_event::reshade_begin_effects>(on_begin_effects);
	reshade::register_overlay("vk-AutoHDR", draw_overlay);
	return true;
}

extern "C" __attribute__((visibility("default"))) void AddonUninit(void *addon_module, void *)
{
	using namespace autohdr;
	// The display may already be gone: leak live watchers, their protocol objects still point at them.
	guarded([&] {
		static auto *const leaked = new std::vector<std::shared_ptr<surface_watcher>>;
		std::vector<std::shared_ptr<surface_watcher>> all;
		{
			std::lock_guard<std::mutex> lock(state().mutex);
			for (auto &e : state().watchers)
				all.push_back(std::move(e.second.watcher));
			state().watchers.clear();
			state().swapchains.clear();
			state().bindings.clear();
		}
		for (const auto &w : all)
		{
			w->leak();
			leaked->push_back(w);
		}
	});
	reshade::unregister_addon(addon_module);
}
