// Manual: needs a live compositor; uses an unmapped wl_surface, nothing is shown. cmake --build build --target watch_probe
#include "surface_watcher.hpp"

#include <poll.h>
#include <wayland-client.h>

#include <chrono>
#include <cstdio>
#include <cstring>

static wl_compositor *compositor = nullptr;

int main()
{
	wl_display *display = wl_display_connect(nullptr);
	if (!display)
	{
		std::fprintf(stderr, "no Wayland display\n");
		return 2;
	}
	wl_registry *registry = wl_display_get_registry(display);
	static const wl_registry_listener listener = {
		[](void *, wl_registry *r, uint32_t name, const char *interface, uint32_t) {
			if (!std::strcmp(interface, "wl_compositor"))
				compositor = static_cast<wl_compositor *>(wl_registry_bind(r, name, &wl_compositor_interface, 1));
		},
		[](void *, wl_registry *, uint32_t) {} };
	wl_registry_add_listener(registry, &listener, nullptr);
	wl_display_roundtrip(display);
	if (!compositor)
	{
		std::fprintf(stderr, "no wl_compositor\n");
		return 2;
	}
	wl_surface *surface = wl_compositor_create_surface(compositor);
	int reports = 0;
	autohdr::surface_watcher::callbacks cb;
	cb.on_report = [&](const autohdr::surface_report &r) {
		++reports;
		const char *why = nullptr;
		const auto v = autohdr::resolve_surface(r, &why);
		if (v)
			std::printf("report: reference %.1f peak %.1f sdr=%d (tf %u)\n", v->reference_white_nits, v->peak_nits, v->sdr_output, r.tf_named.value_or(0));
		else
			std::printf("report rejected: %s\n", why);
	};
	cb.on_unavailable = [] { std::printf("unavailable\n"); };
	cb.on_failed = [] { std::printf("failed\n"); };
	auto watcher = autohdr::surface_watcher::create(surface, cb);

	const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < end)
	{
		while (wl_display_prepare_read(display) != 0)
			wl_display_dispatch_pending(display);
		wl_display_flush(display);
		pollfd pfd = { wl_display_get_fd(display), POLLIN, 0 };
		if (::poll(&pfd, 1, 50) > 0)
			wl_display_read_events(display);
		else
			wl_display_cancel_read(display);
		wl_display_dispatch_pending(display);
		watcher->poll();
	}
	watcher.reset();
	wl_display_roundtrip(display);
	wl_surface_destroy(surface);
	wl_display_disconnect(display);
	std::printf("reports=%d\n", reports);
	return reports > 0 ? 0 : 1;
}
