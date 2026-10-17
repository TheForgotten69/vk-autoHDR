#pragma once

#include "surface_report.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

struct wl_display;
struct wl_event_queue;
struct wl_registry;
struct wl_callback;
struct wl_proxy;
struct wp_color_manager_v1;
struct wp_color_management_surface_feedback_v1;
struct wp_image_description_v1;
struct wp_image_description_info_v1;

namespace autohdr
{
	// Threading: poll() is called on a shared_ptr copy WITHOUT state().mutex, so the destructor never overlaps a poll().
	// Callbacks run inside poll() with mutex_ held and may take state().mutex; never take mutex_ while holding
	// state().mutex. leak() makes poll() and the destructor no-ops (the display may be gone).
	class surface_watcher
	{
	public:
		struct callbacks
		{
			std::function<void(const surface_report &)> on_report;
			std::function<void()> on_unavailable;
			std::function<void()> on_failed;
		};

		static std::shared_ptr<surface_watcher> create(void *wl_surface, callbacks cb);
		~surface_watcher();
		surface_watcher(const surface_watcher &) = delete;
		surface_watcher &operator=(const surface_watcher &) = delete;

		void poll();
		void leak();

	private:
		surface_watcher(void *surface, callbacks cb);

		void request_description();
		void drop_info();
		void drop_description();

		static void on_global(void *, wl_registry *, uint32_t, const char *, uint32_t);
		static void on_sync_done(void *, wl_callback *, uint32_t);
		static void on_preferred_changed(void *, wp_color_management_surface_feedback_v1 *, uint32_t);
		static void on_preferred_changed2(void *, wp_color_management_surface_feedback_v1 *, uint32_t, uint32_t);
		static void on_desc_failed(void *, wp_image_description_v1 *, uint32_t, const char *);
		static void on_desc_ready(void *, wp_image_description_v1 *, uint32_t);
		static void on_desc_ready2(void *, wp_image_description_v1 *, uint32_t, uint32_t);
		static void on_info_done(void *, wp_image_description_info_v1 *);
		static void on_info_tf_named(void *, wp_image_description_info_v1 *, uint32_t);
		static void on_info_luminances(void *, wp_image_description_info_v1 *, uint32_t, uint32_t, uint32_t);
		static void on_info_target_luminance(void *, wp_image_description_info_v1 *, uint32_t, uint32_t);
		static void on_info_target_max_cll(void *, wp_image_description_info_v1 *, uint32_t);

		void *surface_;
		callbacks cb_;
		std::mutex mutex_;
		std::atomic<bool> leaked_{false};
		bool needs_flush_ = false;

		wl_display *display_ = nullptr;
		wl_event_queue *queue_ = nullptr;
		wl_proxy *wrapper_ = nullptr;
		wl_registry *registry_ = nullptr;
		wl_callback *sync_ = nullptr;
		wp_color_manager_v1 *manager_ = nullptr;
		wp_color_management_surface_feedback_v1 *feedback_ = nullptr;
		wp_image_description_v1 *description_ = nullptr;
		wp_image_description_info_v1 *info_ = nullptr;
		surface_report pending_;
	};
}
