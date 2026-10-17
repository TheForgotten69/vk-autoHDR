#include "surface_watcher.hpp"

#include <unistd.h>
#include <wayland-client.h>
#include "color-management-v1-client-protocol.h"

#include <algorithm>
#include <cstring>

namespace autohdr
{
	namespace
	{
		// Every listener member must be filled: libwayland calls them unconditionally.
		void manager_intent(void *, wp_color_manager_v1 *, uint32_t) {}
		void manager_feature(void *, wp_color_manager_v1 *, uint32_t) {}
		void manager_tf(void *, wp_color_manager_v1 *, uint32_t) {}
		void manager_primaries(void *, wp_color_manager_v1 *, uint32_t) {}
		void manager_done(void *, wp_color_manager_v1 *) {}
		const wp_color_manager_v1_listener manager_listener = { manager_intent, manager_feature, manager_tf, manager_primaries, manager_done };

		// The compositor passes an ICC profile as an fd (only for ICC-based descriptions); it must be closed.
		void info_icc_file(void *, wp_image_description_info_v1 *, int32_t fd, uint32_t) { if (fd >= 0) ::close(fd); }
		void info_primaries(void *, wp_image_description_info_v1 *, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t) {}
		void info_primaries_named(void *, wp_image_description_info_v1 *, uint32_t) {}
		void info_tf_power(void *, wp_image_description_info_v1 *, uint32_t) {}
		void info_target_primaries(void *, wp_image_description_info_v1 *, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t) {}
		void info_target_max_fall(void *, wp_image_description_info_v1 *, uint32_t) {}
	}

	std::shared_ptr<surface_watcher> surface_watcher::create(void *wl_surface, callbacks cb)
	{
		return std::shared_ptr<surface_watcher>(new surface_watcher(wl_surface, std::move(cb)));
	}

	surface_watcher::surface_watcher(void *surface, callbacks cb) : surface_(surface), cb_(std::move(cb))
	{
		display_ = wl_proxy_get_display(static_cast<wl_proxy *>(surface));
		queue_ = wl_display_create_queue_with_name(display_, "vk-AutoHDR");
		wrapper_ = static_cast<wl_proxy *>(wl_proxy_create_wrapper(display_));
		wl_proxy_set_queue(wrapper_, queue_);
		static const wl_registry_listener registry_listener = { &surface_watcher::on_global, [](void *, wl_registry *, uint32_t) {} };
		static const wl_callback_listener sync_listener = { &surface_watcher::on_sync_done };
		registry_ = wl_display_get_registry(reinterpret_cast<wl_display *>(wrapper_));
		wl_registry_add_listener(registry_, &registry_listener, this);
		sync_ = wl_display_sync(reinterpret_cast<wl_display *>(wrapper_));
		wl_callback_add_listener(sync_, &sync_listener, this);
		wl_display_flush(display_);
	}

	surface_watcher::~surface_watcher()
	{
		if (leaked_)
			return;
		drop_description();
		if (feedback_)
			wp_color_management_surface_feedback_v1_destroy(feedback_);
		if (manager_)
			wp_color_manager_v1_destroy(manager_);
		if (registry_)
			wl_registry_destroy(registry_);
		if (sync_)
			wl_callback_destroy(sync_);
		wl_proxy_wrapper_destroy(wrapper_);
		wl_event_queue_destroy(queue_);
		wl_display_flush(display_);
	}

	void surface_watcher::leak()
	{
		leaked_ = true;
	}

	void surface_watcher::poll()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (leaked_)
			return;
		wl_display_dispatch_queue_pending(display_, queue_);
		if (needs_flush_)
		{
			needs_flush_ = false;
			wl_display_flush(display_);
		}
	}

	void surface_watcher::drop_info()
	{
		if (info_)
		{
			wl_proxy_destroy(reinterpret_cast<wl_proxy *>(info_));
			info_ = nullptr;
		}
	}

	void surface_watcher::drop_description()
	{
		drop_info();
		if (description_)
		{
			wp_image_description_v1_destroy(description_);
			description_ = nullptr;
		}
	}

	void surface_watcher::request_description()
	{
		static const wp_image_description_v1_listener desc_listener = { &surface_watcher::on_desc_failed, &surface_watcher::on_desc_ready, &surface_watcher::on_desc_ready2 };
		drop_description();
		description_ = wp_color_management_surface_feedback_v1_get_preferred(feedback_);
		wp_image_description_v1_add_listener(description_, &desc_listener, this);
		needs_flush_ = true;
	}

	void surface_watcher::on_global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
	{
		auto *self = static_cast<surface_watcher *>(data);
		if (self->manager_ || std::strcmp(interface, wp_color_manager_v1_interface.name) != 0)
			return;
		static const wp_color_management_surface_feedback_v1_listener feedback_listener = { &surface_watcher::on_preferred_changed, &surface_watcher::on_preferred_changed2 };
		self->manager_ = static_cast<wp_color_manager_v1 *>(wl_registry_bind(registry, name, &wp_color_manager_v1_interface, std::min(version, 2u)));
		wp_color_manager_v1_add_listener(self->manager_, &manager_listener, self);
		self->feedback_ = wp_color_manager_v1_get_surface_feedback(self->manager_, static_cast<wl_surface *>(self->surface_));
		wp_color_management_surface_feedback_v1_add_listener(self->feedback_, &feedback_listener, self);
		self->request_description();
	}

	void surface_watcher::on_sync_done(void *data, wl_callback *callback, uint32_t)
	{
		auto *self = static_cast<surface_watcher *>(data);
		wl_callback_destroy(callback);
		self->sync_ = nullptr;
		if (!self->manager_)
			self->cb_.on_unavailable();
	}

	void surface_watcher::on_preferred_changed(void *data, wp_color_management_surface_feedback_v1 *, uint32_t)
	{
		static_cast<surface_watcher *>(data)->request_description();
	}

	void surface_watcher::on_preferred_changed2(void *data, wp_color_management_surface_feedback_v1 *, uint32_t, uint32_t)
	{
		static_cast<surface_watcher *>(data)->request_description();
	}

	void surface_watcher::on_desc_failed(void *data, wp_image_description_v1 *, uint32_t, const char *)
	{
		static_cast<surface_watcher *>(data)->cb_.on_failed();
	}

	void surface_watcher::on_desc_ready(void *data, wp_image_description_v1 *description, uint32_t)
	{
		static const wp_image_description_info_v1_listener info_listener = {
			&surface_watcher::on_info_done, info_icc_file, info_primaries, info_primaries_named, info_tf_power,
			&surface_watcher::on_info_tf_named, &surface_watcher::on_info_luminances, info_target_primaries,
			&surface_watcher::on_info_target_luminance, &surface_watcher::on_info_target_max_cll, info_target_max_fall };
		auto *self = static_cast<surface_watcher *>(data);
		self->drop_info();
		self->pending_ = surface_report{};
		self->info_ = wp_image_description_v1_get_information(description);
		wp_image_description_info_v1_add_listener(self->info_, &info_listener, self);
		self->needs_flush_ = true;
	}

	void surface_watcher::on_desc_ready2(void *data, wp_image_description_v1 *description, uint32_t, uint32_t)
	{
		on_desc_ready(data, description, 0);
	}

	void surface_watcher::on_info_done(void *data, wp_image_description_info_v1 *)
	{
		auto *self = static_cast<surface_watcher *>(data);
		self->drop_info();
		const surface_report report = self->pending_;
		self->pending_ = surface_report{};
		self->cb_.on_report(report);
	}

	void surface_watcher::on_info_tf_named(void *data, wp_image_description_info_v1 *, uint32_t tf)
	{
		static_cast<surface_watcher *>(data)->pending_.tf_named = tf;
	}

	void surface_watcher::on_info_luminances(void *data, wp_image_description_info_v1 *, uint32_t, uint32_t max_lum, uint32_t reference_lum)
	{
		surface_report &r = static_cast<surface_watcher *>(data)->pending_;
		r.max_lum = max_lum;
		r.reference_lum = reference_lum;
	}

	void surface_watcher::on_info_target_luminance(void *data, wp_image_description_info_v1 *, uint32_t, uint32_t max_lum)
	{
		static_cast<surface_watcher *>(data)->pending_.target_max_lum = max_lum;
	}

	void surface_watcher::on_info_target_max_cll(void *data, wp_image_description_info_v1 *, uint32_t max_cll)
	{
		static_cast<surface_watcher *>(data)->pending_.target_max_cll = max_cll;
	}
}
