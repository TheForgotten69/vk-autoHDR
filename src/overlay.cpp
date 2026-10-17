#include <imgui.h>
#include <reshade.hpp>

#include "addon_state.hpp"
#include "addon_util.hpp"
#include "binder.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>

namespace autohdr
{
	namespace
	{
		const ImVec4 color_ok(0.40f, 0.85f, 0.45f, 1.f);
		const ImVec4 color_warn(1.00f, 0.80f, 0.25f, 1.f);
		const ImVec4 color_bad(1.00f, 0.45f, 0.40f, 1.f);

		ImVec4 color_muted()
		{
			return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
		}

		void dot(const ImVec4 &color)
		{
			const float size = ImGui::GetFontSize() * 0.7f;
			ImGui::ColorButton("##dot", color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_NoBorder, ImVec2(size, size));
			ImGui::SameLine();
		}

		void tooltip(const char *text)
		{
			if (ImGui::BeginItemTooltip())
			{
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.f);
				ImGui::TextUnformatted(text);
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
		}

		void wrapped_colored(const ImVec4 &color, const char *text)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::TextWrapped("%s", text);
			ImGui::PopStyleColor();
		}

		void row(const char *label, const char *fmt, ...) IM_FMTARGS(2);
		void row(const char *label, const char *fmt, ...)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", label);
			ImGui::TableNextColumn();
			char buf[256];
			va_list args;
			va_start(args, fmt);
			std::vsnprintf(buf, sizeof(buf), fmt, args);
			va_end(args);
			ImGui::TextUnformatted(buf);
		}

		bool begin_kv_table(const char *id)
		{
			if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit))
				return false;
			ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 9.f);
			ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
			return true;
		}

		void draw_status(const std::optional<decision> &status, const std::string &x11_advice_line)
		{
			if (!status)
			{
				dot(color_muted());
				ImGui::TextDisabled("Waiting for a swap chain");
				return;
			}
			if (status->engage)
			{
				dot(color_ok);
				ImGui::TextColored(color_ok, "HDR active");
				ImGui::SameLine();
				ImGui::TextDisabled("scRGB output");
			}
			else
			{
				dot(color_muted());
				ImGui::TextUnformatted("Not active");
				ImGui::SameLine();
				ImGui::TextDisabled("%s", reason_text(status->reason));
			}
			if (!x11_advice_line.empty())
				wrapped_colored(color_warn, x11_advice_line.c_str());
		}

		void draw_display(const effective_display &eff, const autohdr_config &cfg)
		{
			const display_info &d = eff.info;
			ImGui::SeparatorText("Display");
			if (begin_kv_table("##display"))
			{
				const bool from_compositor = d.source == display_source::compositor;
				if (from_compositor)
					row("Screen", "this window's screen");
				else
					row("Screen", "%s", d.connector.empty() ? "unknown" : d.connector.c_str());
				tooltip(from_compositor ? "Reported by the compositor for the screen the window is on; follows the window when it moves."
				                       : "Fallback: read from KWin's output config or the monitor's EDID, not from the window's screen.");
				row("Source", "%s", source_text(d.source));
				row("Reference white", "%.0f nits%s", d.reference_white_nits, d.reference_white_assumed ? "  (assumed)" : "");
				row("Peak", "%.0f nits%s", d.peak_nits, d.peak_assumed ? "  (assumed)" : "");
				row("Game white", "%.0f nits", game_white(d, cfg));
				tooltip("SDR white in the game: the Game white setting, capped at the reference white, times SDR white scale.");

				const double hr = headroom(d, cfg);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextDisabled("Headroom");
				ImGui::TableNextColumn();
				char overlay[32];
				std::snprintf(overlay, sizeof(overlay), "%.2fx", hr);
				const bool low = hr < headroom_warning_threshold;
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, low ? color_warn : color_ok);
				ImGui::ProgressBar(static_cast<float>(std::clamp((hr - 1.0) / 3.0, 0.0, 1.0)), ImVec2(-1.f, 0.f), overlay);
				ImGui::PopStyleColor();
				ImGui::EndTable();
			}
			if (headroom(d, cfg) < headroom_warning_threshold)
				wrapped_colored(color_warn, "Low headroom: game white is close to the screen's peak. Lower Game white (203 is the HDR standard) for a visible effect.");
		}

		void draw_techniques(reshade::api::effect_runtime *runtime, bool engaged, int mask_count)
		{
			ImGui::SeparatorText("Techniques");
			const technique_snapshot snap = take_technique_snapshot(runtime);
			if (ImGui::BeginTable("##techniques", 2, ImGuiTableFlags_SizingFixedFit))
			{
				ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 11.f);
				ImGui::TableSetupColumn("state", ImGuiTableColumnFlags_WidthStretch);
				const auto line = [](const char *name, const technique_status &st) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(name);
					ImGui::TableNextColumn();
					if (!st.found)
						ImGui::TextColored(color_bad, "missing");
					else if (st.enabled)
						ImGui::TextColored(color_ok, "on");
					else
						ImGui::TextDisabled("off");
				};
				std::pair<const char *, const technique_status *> rows[] = {
					{ "Deband", &snap.deband },
					{ "AutoHDR_UI_Capture", &snap.ui_capture },
					{ "AdvancedAutoHDR", &snap.pumbo },
					{ "AutoHDR_UI_Apply", &snap.ui_apply },
				};
				std::stable_sort(std::begin(rows), std::end(rows), [](const auto &a, const auto &b) {
					return static_cast<unsigned>(a.second->order) < static_cast<unsigned>(b.second->order); // -1 sorts last
				});
				for (const auto &[name, st] : rows)
					line(name, *st);
				ImGui::EndTable();
			}

			const std::vector<advice> list = advise(snap, engaged, mask_count);
			for (const advice a : list)
			{
				ImGui::Bullet();
				wrapped_colored(is_warning(a) ? color_warn : color_muted(), advice_text(a));
			}
			if (list.empty())
				ImGui::TextDisabled("All set. Toggle techniques in the Home tab; the add-on never changes them.");
		}

		void draw_panel(reshade::api::effect_runtime *runtime)
		{
			static autohdr_config cfg;
			static bool have_draft = false;
			effective_display eff;
			pumbo_values values;
			bool outlines;
			int mask_count = 0;
			std::optional<decision> status;
			std::string x11_advice_line;
			{
				std::lock_guard<std::mutex> lock(state().mutex);
				if (!have_draft)
					cfg = state().config;
				eff = effective_display_locked(state(), runtime->get_native());
				values = compute_pumbo_values(eff.info, state().config, eff.scrgb_white);
				mask_count = state().ui.count;
				outlines = state().show_outlines;
				const auto it = state().swapchains.find(runtime->get_native());
				if (it != state().swapchains.end())
				{
					status = it->second.status;
					if (is_x11_stepped_aside(it->second))
						x11_advice_line = state().x11_advice_line;
				}
			}

			have_draft = true;
			bool changed = false, redetect = false;
			const auto committed = [] { return ImGui::IsItemDeactivatedAfterEdit(); };

			draw_status(status, x11_advice_line);
			draw_display(eff, cfg);
			if (status)
				draw_techniques(runtime, status->engage, mask_count);

			ImGui::SeparatorText("Settings");
			changed |= ImGui::Checkbox("Enable AutoHDR", &cfg.enable);
			tooltip("Applies on the next swap chain recreation (resize or fullscreen toggle).");
			ImGui::InputFloat("Game white (nits, 0 = screen)", &cfg.game_white_nits, 0.f, 0.f, "%.0f");
			changed |= committed();
			tooltip("SDR white for the game in nits, never above the screen's reference white (KDE's SDR brightness).\n203 is the HDR reference white (BT.2408); 0 matches the desktop.");
			ImGui::SliderFloat("SDR white scale", &cfg.sdr_white_scale, 0.25f, 4.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			changed |= committed();
			tooltip("Multiplies the game white above.");
			ImGui::InputFloat("Max nits (0 = auto)", &cfg.max_nits_override, 0.f, 0.f, "%.0f");
			changed |= committed();
			tooltip("Highlight ceiling in physical nits. 0 derives it from the screen's peak.");

			if (eff.info.source != display_source::compositor && eff.info.hdr_connectors.size() > 1)
			{
				if (ImGui::BeginCombo("Screen", cfg.connector.empty() ? "Auto" : cfg.connector.c_str()))
				{
					if (ImGui::Selectable("Auto", cfg.connector.empty()) && !cfg.connector.empty())
					{
						cfg.connector.clear();
						changed = redetect = true;
					}
					for (const std::string &c : eff.info.hdr_connectors)
					{
						if (ImGui::Selectable(c.c_str(), c == cfg.connector) && c != cfg.connector)
						{
							cfg.connector = c;
							changed = redetect = true;
						}
					}
					ImGui::EndCombo();
				}
			}
			if (ImGui::Button("Re-detect"))
				changed = redetect = true;
			tooltip("Re-read KWin's output config and the EDIDs (the fallback values).");

			if (ImGui::CollapsingHeader("Manual display values"))
			{
				if (ImGui::Checkbox("Use manual values", &cfg.use_manual))
					changed = redetect = true;
				ImGui::BeginDisabled(!cfg.use_manual);
				ImGui::InputFloat("Reference white (nits)", &cfg.reference_white_nits, 0.f, 0.f, "%.0f");
				if (committed())
					changed = redetect = true;
				ImGui::InputFloat("Peak (nits)", &cfg.peak_nits, 0.f, 0.f, "%.0f");
				if (committed())
					changed = redetect = true;
				ImGui::EndDisabled();
			}

			if (ImGui::CollapsingHeader("UI masks"))
			{
				ImGui::SliderFloat("UI white scale", &cfg.ui_white_scale, 0.25f, 4.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
				changed |= committed();
				tooltip("Brightness of masked HUD areas, which stay at SDR white instead of being expanded.");
				bool new_outlines = outlines;
				if (ImGui::Checkbox("Show mask outlines", &new_outlines))
					set_show_outlines(new_outlines);

				int remove = -1;
				for (size_t i = 0; i < cfg.masks.size(); ++i)
				{
					ImGui::PushID(static_cast<int>(i));
					char label[32];
					std::snprintf(label, sizeof(label), "Mask %d", static_cast<int>(i) + 1);
					ImGui::InputFloat4(label, &cfg.masks[i].x0, "%.3f");
					changed |= committed();
					tooltip("x0, y0, x1, y1 in normalized screen coordinates (0..1).");
					ImGui::SameLine();
					if (ImGui::SmallButton("Remove"))
						remove = static_cast<int>(i);
					ImGui::PopID();
				}
				if (remove >= 0)
				{
					cfg.masks.erase(cfg.masks.begin() + remove);
					changed = true;
				}
				ImGui::BeginDisabled(cfg.masks.size() >= max_masks);
				if (ImGui::Button("Add mask"))
				{
					cfg.masks.push_back({ 0.f, 0.f, 0.1f, 0.1f });
					changed = true;
				}
				ImGui::EndDisabled();
			}

			if (ImGui::CollapsingHeader("Computed shader values"))
			{
				if (begin_kv_table("##values"))
				{
					for (const uniform_assignment &a : pumbo_assignments(values))
						row(a.name, "%g", static_cast<double>(a.value));
					row("scRGB reference white", "%g nits%s", eff.scrgb_white, cfg.scrgb_white_nits > 0 ? "  (ScrgbWhiteNits)" : "");
					tooltip("scRGB nits (1.0 = 80) the compositor shows at the screen's reference white: 203 for NVIDIA's Wayland driver, 80 for Mesa.");
					ImGui::EndTable();
				}
				ImGui::TextDisabled("Written to AdvancedAutoHDR by this panel; edits in the effect list are overwritten.");
			}

			if (changed)
				commit_config(cfg, redetect);
			if (changed || !ImGui::IsAnyItemActive())
				have_draft = false;
		}
	}

	void draw_overlay(reshade::api::effect_runtime *runtime)
	{
		guarded([&] { draw_panel(runtime); });
	}
}
