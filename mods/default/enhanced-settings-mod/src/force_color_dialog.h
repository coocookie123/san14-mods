#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "san14_host.h"

namespace enhanced_settings_mod {

class ForceColorDialog {
public:
    static constexpr uint32_t kPaletteSize = SAN14_FORCE_COLOR_PALETTE_SIZE;
    static constexpr size_t kForcesPerPage = 8;

    static bool supported(const San14HostApi* h) {
        return h &&
               h->struct_size >= offsetof(San14HostApi, force_colors) + sizeof(h->force_colors) &&
               h->force_colors &&
               h->force_colors->struct_size >= SAN14_FORCE_COLOR_API_V1_SIZE &&
               h->force_colors->read_palette &&
               h->force_colors->list_forces &&
               h->force_colors->set_color &&
               h->ui.struct_size >= sizeof(San14UiApi) &&
               h->ui.create_panel && h->ui.add_label && h->ui.add_button &&
               h->ui.set_text && h->ui.show_panel && h->ui.close_panel &&
               h->ui_style &&
               h->ui_style->struct_size >= offsetof(San14UiStyleApi, bind_help) + sizeof(h->ui_style->bind_help) &&
               (h->ui_style->capabilities & SAN14_UI_STYLE_EXPERT) &&
               h->ui_style->set_selected &&
               h->ui_style->bind_help;
    }

    static bool tint_supported(const San14HostApi* h) {
        return h && h->ui_style &&
               (h->ui_style->capabilities & SAN14_UI_STYLE_TINT) &&
               h->ui_style->struct_size >= offsetof(San14UiStyleApi, set_tint) + sizeof(h->ui_style->set_tint) &&
               h->ui_style->set_tint;
    }

    static bool enabled_supported(const San14HostApi* h) {
        return h && h->ui_style &&
               h->ui_style->struct_size >= offsetof(San14UiStyleApi, set_enabled) + sizeof(h->ui_style->set_enabled) &&
               (h->ui_style->capabilities & SAN14_UI_STYLE_ENABLED) && h->ui_style->set_enabled;
    }

    static uint32_t swatch_argb(const San14ForceColorSwatch& swatch) {
        return 0xff000000u |
               (static_cast<uint32_t>(swatch.red) << 16) |
               (static_cast<uint32_t>(swatch.green) << 8) |
               static_cast<uint32_t>(swatch.blue);
    }

    void bind(const San14HostApi* h) {
        host_ = h;
    }

    void reset() {
        if (host_ && panel_ && host_->ui.close_panel) {
            host_->ui.close_panel(host_->host_context, panel_);
        }
        clear_handles();
        undo_ = {};
        forces_.clear();
        selected_force_id_ = 0;
        force_page_ = 0;
        status_text_.clear();
    }

    San14Result open(uint32_t initial_force_id = 0) {
        if (!supported(host_) || !enabled_supported(host_)) return SAN14_E_UNSUPPORTED;
        if (panel_) {
            host_->ui.close_panel(host_->host_context, panel_);
            clear_handles();
        }
        status_text_.clear();
        refresh_data();
        if(initial_force_id) {
            const auto result=select_force(initial_force_id);
            if(result!=SAN14_OK)return result;
        }
        return build_panel();
    }

    San14Result select_force(uint32_t force_id) {
        auto it=std::find_if(forces_.begin(),forces_.end(),[&](const auto& f){return f.entry.force_id==force_id;});
        if(it==forces_.end())return SAN14_E_NOT_FOUND;
        selected_force_id_=force_id;
        force_page_=static_cast<size_t>(it-forces_.begin())/kForcesPerPage;
        if(panel_)update_controls();
        return SAN14_OK;
    }

    San14PanelHandle panel() const { return panel_; }

private:
    struct ForceItem {
        San14ForceColorEntry entry{};
        std::string display_name;
    };

    struct UndoState {
        bool valid = false;
        uint32_t force_id = 0;
        uint8_t prev_color = 0;
        uint64_t generation = 0;
        bool has_generation = false;
    };

    struct RowControls {
        San14ControlHandle swatch = 0;
        San14ControlHandle button = 0;
        uint32_t force_id = 0;
    };

    const San14HostApi* host_ = nullptr;
    San14PanelHandle panel_ = 0;
    San14ControlHandle help_label_ = 0;
    San14ControlHandle status_label_ = 0;
    San14ControlHandle page_label_ = 0;
    San14ControlHandle prev_page_btn_ = 0;
    San14ControlHandle next_page_btn_ = 0;
    San14ControlHandle undo_btn_ = 0;

    std::array<San14ForceColorSwatch, kPaletteSize> palette_{};
    bool palette_loaded_ = false;
    std::vector<ForceItem> forces_;
    std::array<RowControls, kForcesPerPage> rows_{};
    std::array<San14ControlHandle, kPaletteSize> color_swatches_{};
    std::array<San14ControlHandle, kPaletteSize> color_buttons_{};

    uint32_t selected_force_id_ = 0;
    size_t force_page_ = 0;
    UndoState undo_{};
    std::string status_text_;

    static San14StringView sv(const char* text) {
        return {text, static_cast<uint32_t>(std::strlen(text))};
    }

    static San14StringView sv(const std::string& text) {
        return {text.c_str(), static_cast<uint32_t>(text.size())};
    }

    static std::string color_tag(uint8_t color_id) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02u", static_cast<unsigned>(color_id));
        return std::string(buf);
    }

    void clear_handles() {
        panel_ = 0;
        help_label_ = 0;
        status_label_ = 0;
        page_label_ = 0;
        prev_page_btn_ = 0;
        next_page_btn_ = 0;
        undo_btn_ = 0;
        rows_ = {};
        color_swatches_ = {};
        color_buttons_ = {};
    }

    bool query_generation(uint64_t& out_gen) const {
        if (!host_ ||
            host_->struct_size < offsetof(San14HostApi, world) + sizeof(host_->world) ||
            !host_->world ||
            host_->world->struct_size < offsetof(San14WorldApi, current_generation) + sizeof(host_->world->current_generation) ||
            !host_->world->current_generation) {
            return false;
        }
        return host_->world->current_generation(host_->host_context, &out_gen) == SAN14_OK;
    }

    std::map<uint32_t, std::string> capture_force_names() const {
        std::map<uint32_t, std::string> names;
        if (!host_ ||
            host_->struct_size < offsetof(San14HostApi, world) + sizeof(host_->world) ||
            !host_->world ||
            host_->world->struct_size < sizeof(San14WorldApi) ||
            !host_->world->capture || !host_->world->forces || !host_->world->release) {
            return names;
        }
        San14Handle cap = 0;
        San14WorldInfo info{};
        info.struct_size = sizeof(info);
        if (host_->world->capture(host_->host_context, SAN14_WORLD_FORCES, &cap, &info) != SAN14_OK || !cap) {
            return names;
        }
        if (info.force_count > 0) {
            std::vector<San14ForceSnapshot> snaps(info.force_count);
            for (auto& s : snaps) s.struct_size = sizeof(s);
            uint32_t out_count = 0;
            if (host_->world->forces(host_->host_context, cap, 0, snaps.data(), info.force_count,
                                     static_cast<uint32_t>(sizeof(San14ForceSnapshot)), &out_count) == SAN14_OK) {
                for (uint32_t i = 0; i < out_count; ++i) {
                    if (snaps[i].name_utf8[0]) {
                        names[snaps[i].id] = snaps[i].name_utf8;
                    }
                }
            }
        }
        host_->world->release(host_->host_context, cap);
        return names;
    }

    const ForceItem* find_force(uint32_t force_id) const {
        for (const auto& f : forces_) {
            if (f.entry.force_id == force_id) return &f;
        }
        return nullptr;
    }

    const ForceItem* find_color_owner(uint8_t color_id, uint32_t exclude_force_id) const {
        for (const auto& f : forces_) {
            if (f.entry.force_id != exclude_force_id && f.entry.color_id == color_id) {
                return &f;
            }
        }
        return nullptr;
    }

    void refresh_data() {
        palette_loaded_ = false;
        uint32_t palette_count = 0;
        if (host_->force_colors->read_palette(host_->host_context, palette_.data(), kPaletteSize, &palette_count) == SAN14_OK &&
            palette_count == kPaletteSize) {
            palette_loaded_ = true;
        } else {
            for (uint32_t i = 0; i < kPaletteSize; ++i) {
                palette_[i] = {sizeof(San14ForceColorSwatch), static_cast<uint8_t>(i), 200, 200, 200, 255, {}};
            }
        }

        forces_.clear();
        uint32_t force_count = 0;
        const auto probe = host_->force_colors->list_forces(host_->host_context, nullptr, 0, &force_count);
        if (probe == SAN14_E_NOT_FOUND) {
            selected_force_id_ = 0;
            status_text_ = u8"当前未进入战局，请载入存档或开始游戏后再修改势力颜色。";
            return;
        }
        if ((probe != SAN14_OK && probe != SAN14_E_BUFFER_TOO_SMALL)) {
            selected_force_id_ = 0;
            status_text_ = u8"无法读取势力颜色列表，请确认优化伴侣已启用并处于战局中。";
            return;
        }
        if (force_count == 0) {
            selected_force_id_ = 0;
            status_text_ = u8"当前战局未检测到可编辑颜色的活跃势力。";
            return;
        }

        std::vector<San14ForceColorEntry> entries(force_count);
        for (auto& e : entries) e.struct_size = sizeof(e);
        uint32_t read_count = 0;
        if (host_->force_colors->list_forces(host_->host_context, entries.data(), force_count, &read_count) != SAN14_OK) {
            selected_force_id_ = 0;
            status_text_ = u8"读取活跃势力列表失败。";
            return;
        }
        entries.resize(read_count);
        const auto names = capture_force_names();

        forces_.reserve(entries.size());
        for (const auto& entry : entries) {
            auto it = names.find(entry.force_id);
            std::string base_name = (it != names.end() && !it->second.empty())
                                        ? it->second
                                        : (std::string(u8"势力 #") + std::to_string(entry.force_id));
            if (entry.is_tribal) base_name = std::string(u8"[异族] ") + base_name;
            forces_.push_back({entry, base_name});
        }

        if (!find_force(selected_force_id_) && !forces_.empty()) {
            selected_force_id_ = forces_.front().entry.force_id;
            force_page_ = 0;
        }
        if (undo_.valid) {
            uint64_t gen = 0;
            if (undo_.has_generation && (!query_generation(gen) || gen != undo_.generation)) {
                undo_.valid = false;
            }
        }
        if (status_text_.empty()) {
            if (const auto* sel = find_force(selected_force_id_)) {
                status_text_ = std::string(u8"已选中【") + sel->display_name +
                               u8"】（当前颜色 #" + color_tag(sel->entry.color_id) +
                               u8"），请点击右侧空闲色块应用修改。";
            }
        }
    }

    San14Result build_panel() {
        San14PanelHandle next = 0;
        try {
            auto check = [](San14Result r) {
                if (r != SAN14_OK) throw r;
            };
            const San14PanelSpec pspec{
                sizeof(pspec), sv(u8"更改势力颜色"),
                SAN14_EXPERT_WIDTH, SAN14_EXPERT_HEIGHT, SAN14_PANEL_EXPERT
            };
            check(host_->ui.create_panel(host_->host_context, &pspec, &next));

            auto make_label = [&](const char* id, int32_t x, int32_t y, int32_t w, int32_t h,
                                  const std::string& text, uint32_t flags) -> San14ControlHandle {
                const San14ControlSpec spec{sizeof(spec), sv(id), sv(text), x, y, w, h, flags};
                San14ControlHandle handle = 0;
                check(host_->ui.add_label(host_->host_context, next, &spec, &handle));
                return handle;
            };
            auto make_button = [&](const char* id, int32_t x, int32_t y, int32_t w, int32_t h,
                                   const std::string& text, uint32_t flags,
                                   San14ControlCallback cb, const std::string& help_desc) -> San14ControlHandle {
                const San14ControlSpec spec{sizeof(spec), sv(id), sv(text), x, y, w, h, flags};
                San14ControlHandle handle = 0;
                check(host_->ui.add_button(host_->host_context, next, &spec, cb, this, &handle));
                if (help_label_ && !help_desc.empty()) {
                    check(host_->ui_style->bind_help(host_->host_context, handle, help_label_, sv(help_desc)));
                }
                return handle;
            };

            help_label_ = make_label("fc_help", SAN14_EXPERT_LEFT + 24, SAN14_EXPERT_HELP_TOP + 18,
                                     SAN14_EXPERT_ROW_WIDTH - 48, 100, "", SAN14_CONTROL_EXPERT_LABEL);

            make_label("fc_hdr_left", 160, 106, 420, 34, u8"活跃势力列表（含异族势力）", SAN14_CONTROL_EXPERT_LABEL);
            make_label("fc_hdr_right", 610, 106, 1160, 34,
                       u8"60 色调色板（★ 为当前势力颜色）", SAN14_CONTROL_EXPERT_LABEL);

            for (size_t i = 0; i < kForcesPerPage; ++i) {
                const int32_t y = 148 + static_cast<int32_t>(i) * 62;
                char sid[24], bid[24];
                std::snprintf(sid, sizeof(sid), "fc_rsw_%zu", i);
                std::snprintf(bid, sizeof(bid), "fc_row_%zu", i);
                rows_[i].swatch = make_label(sid, 160, y + 10, 32, 32, u8"■", SAN14_CONTROL_EXPERT_LABEL);
                rows_[i].button = make_button(bid, 200, y + 4, 380, 44, "", SAN14_CONTROL_TAB,
                                              on_force_click, u8"选择要修改颜色的势力（包含异民族势力）。");
            }

            prev_page_btn_ = make_button("fc_prev", 160, 656, 130, 40, u8"上一页", 0,
                                         on_prev_page, u8"查看上一页活跃势力。");
            page_label_ = make_label("fc_page", 300, 662, 140, 32, "1 / 1", SAN14_CONTROL_EXPERT_LABEL);
            next_page_btn_ = make_button("fc_next", 450, 656, 130, 40, u8"下一页", 0,
                                         on_next_page, u8"查看下一页活跃势力。");

            const bool can_tint = tint_supported(host_);
            for (uint32_t c = 0; c < kPaletteSize; ++c) {
                const int32_t col = static_cast<int32_t>(c % 10);
                const int32_t row = static_cast<int32_t>(c / 10);
                const int32_t x = 610 + col * 116;
                const int32_t y = 148 + row * 84;
                char sid[24], bid[24];
                std::snprintf(sid, sizeof(sid), "fc_psw_%u", c);
                std::snprintf(bid, sizeof(bid), "fc_pbt_%u", c);
                color_swatches_[c] = make_label(sid, x, y + 10, 30, 32, u8"■", SAN14_CONTROL_EXPERT_LABEL);
                if (can_tint) {
                    host_->ui_style->set_tint(host_->host_context, color_swatches_[c], swatch_argb(palette_[c]));
                }
                color_buttons_[c] = make_button(bid, x + 32, y + 4, 74, 42, color_tag(static_cast<uint8_t>(c)),
                                                SAN14_CONTROL_TAB, on_palette_click,
                                                u8"点击将当前选中势力更改为此颜色。");
            }

            status_label_ = make_label("fc_status", 160, 706, 1600, 36, status_text_, SAN14_CONTROL_EXPERT_LABEL);
            undo_btn_ = make_button("fc_undo", 160, 980, 220, 44, u8"撤销上次修改", 0,
                                    on_undo_click, u8"撤销本次战局中最近一次势力颜色修改。");
            make_button("fc_refresh", 400, 980, 160, 44, u8"刷新列表", 0,
                        on_refresh_click, u8"重新读取当前战局势力及地图颜色。");
            make_label("fc_hint", 360, 918, 1200, 30,
                       u8"修改即时写入并刷新地图 · 可随时撤销最近一次更改 · Esc 返回设置页", 0);
            make_button("fc_close", 904, SAN14_EXPERT_FOOTER_TOP, 112, 112, u8"返回",
                        SAN14_CONTROL_CONFIRM, on_close_click, u8"返回上一页。");

            panel_ = next;
            update_controls();
            check(host_->ui.show_panel(host_->host_context, panel_));
            return SAN14_OK;
        } catch (San14Result err) {
            if (next) host_->ui.close_panel(host_->host_context, next);
            clear_handles();
            return err;
        } catch (...) {
            if (next) host_->ui.close_panel(host_->host_context, next);
            clear_handles();
            return SAN14_E_PLUGIN_FAILED;
        }
    }

    void update_controls() {
        if (!panel_) return;
        const bool can_tint = tint_supported(host_);
        const size_t total_pages = forces_.empty() ? 1 : (forces_.size() + kForcesPerPage - 1) / kForcesPerPage;
        if (force_page_ >= total_pages) force_page_ = total_pages - 1;
        const size_t start = force_page_ * kForcesPerPage;

        for (size_t i = 0; i < kForcesPerPage; ++i) {
            const size_t idx = start + i;
            if (idx < forces_.size()) {
                const auto& item = forces_[idx];
                rows_[i].force_id = item.entry.force_id;
                const std::string label = item.display_name + " (#" + color_tag(item.entry.color_id) + ")";
                host_->ui.set_text(host_->host_context, rows_[i].button, sv(label));
                host_->ui_style->set_selected(host_->host_context, rows_[i].button,
                                              item.entry.force_id == selected_force_id_ ? 1 : 0);
                host_->ui.set_text(host_->host_context, rows_[i].swatch, sv(u8"■"));
                if (can_tint && item.entry.color_id < kPaletteSize) {
                    host_->ui_style->set_tint(host_->host_context, rows_[i].swatch,
                                              swatch_argb(palette_[item.entry.color_id]));
                }
            } else {
                rows_[i].force_id = 0;
                host_->ui.set_text(host_->host_context, rows_[i].button, sv("-"));
                host_->ui_style->set_selected(host_->host_context, rows_[i].button, 0);
                host_->ui.set_text(host_->host_context, rows_[i].swatch, sv(""));
            }
        }

        const std::string page_str = std::to_string(force_page_ + 1) + " / " + std::to_string(total_pages);
        host_->ui.set_text(host_->host_context, page_label_, sv(page_str));

        const ForceItem* selected = find_force(selected_force_id_);
        for (uint32_t c = 0; c < kPaletteSize; ++c) {
            const uint8_t cid = static_cast<uint8_t>(c);
            const bool is_current = selected && selected->entry.color_id == cid;
            const ForceItem* owner = find_color_owner(cid, selected_force_id_);
            std::string btn_label;
            std::string help_desc;
            if (is_current) {
                btn_label = std::string(u8"★") + color_tag(cid);
                help_desc = std::string(u8"颜色 #") + color_tag(cid) + u8"：当前选中势力正在使用。";
            } else if (owner) {
                btn_label = color_tag(cid);
            } else {
                btn_label = color_tag(cid);
                help_desc = std::string(u8"颜色 #") + color_tag(cid) + u8"：空闲可用，点击即可应用并刷新地图。";
            }
            host_->ui.set_text(host_->host_context, color_buttons_[c], sv(btn_label));
            host_->ui_style->set_selected(host_->host_context, color_buttons_[c], is_current ? 1 : 0);
            host_->ui_style->set_enabled(host_->host_context, color_buttons_[c], selected && !owner ? 1 : 0);
            if (can_tint) {
                host_->ui_style->set_tint(host_->host_context, color_swatches_[c],
                                        selected && !owner ? swatch_argb(palette_[c]) : 0xff808080u);
            }
            host_->ui_style->bind_help(host_->host_context, color_buttons_[c], help_label_, sv(help_desc));
        }

        host_->ui.set_text(host_->host_context, status_label_, sv(status_text_));
    }

    void handle_select_force(San14ControlHandle control) {
        for (const auto& row : rows_) {
            if (row.button == control && row.force_id != 0) {
                selected_force_id_ = row.force_id;
                if (const auto* f = find_force(selected_force_id_)) {
                    status_text_ = std::string(u8"已选中【") + f->display_name +
                                   u8"】（当前颜色 #" + color_tag(f->entry.color_id) +
                                   u8"），请点击右侧空闲颜色应用。";
                }
                update_controls();
                return;
            }
        }
    }

    void handle_pick_color(San14ControlHandle control) {
        uint8_t picked = kPaletteSize;
        for (uint32_t c = 0; c < kPaletteSize; ++c) {
            if (color_buttons_[c] == control) {
                picked = static_cast<uint8_t>(c);
                break;
            }
        }
        if (picked >= kPaletteSize) return;
        const ForceItem* sel = find_force(selected_force_id_);
        if (!sel) {
            status_text_ = u8"请先选择一个活跃势力。";
            update_controls();
            return;
        }
        if (sel->entry.color_id == picked) {
            status_text_ = std::string(u8"【") + sel->display_name + u8"】当前已在使用颜色 #" + color_tag(picked) + u8"。";
            update_controls();
            return;
        }
        if (find_color_owner(picked, selected_force_id_)) return;

        uint8_t prev = 0;
        const auto res = host_->force_colors->set_color(host_->host_context, selected_force_id_, picked, &prev);
        if (res == SAN14_E_BUSY) {
            status_text_.clear();
            refresh_data();
            update_controls();
            return;
        }
        if (res != SAN14_OK) {
            status_text_ = std::string(u8"修改势力颜色失败（错误码 ") + std::to_string(res) + u8"）。";
            refresh_data();
            update_controls();
            return;
        }

        uint64_t gen = 0;
        const bool has_gen = query_generation(gen);
        undo_ = {true, selected_force_id_, prev, gen, has_gen};
        const std::string name = sel->display_name;
        status_text_ = std::string(u8"已将【") + name + u8"】从 #" + color_tag(prev) +
                       u8" 更改为 #" + color_tag(picked) + u8"，地图配色已刷新。";
        refresh_data();
        update_controls();
    }

    void handle_undo() {
        if (!undo_.valid) {
            status_text_ = u8"当前没有可撤销的颜色修改记录。";
            update_controls();
            return;
        }
        uint64_t gen = 0;
        if (undo_.has_generation && (!query_generation(gen) || gen != undo_.generation)) {
            undo_.valid = false;
            status_text_ = u8"战局已切换或重新读档，原颜色撤销记录已失效。";
            update_controls();
            return;
        }
        const auto res = host_->force_colors->set_color(host_->host_context, undo_.force_id, undo_.prev_color, nullptr);
        if (res == SAN14_E_BUSY) {
            status_text_ = std::string(u8"原颜色 #") + color_tag(undo_.prev_color) + u8" 已被其他势力占用，无法撤销。";
            refresh_data();
            update_controls();
            return;
        }
        if (res != SAN14_OK) {
            status_text_ = std::string(u8"撤销失败（错误码 ") + std::to_string(res) + u8"）。";
            refresh_data();
            update_controls();
            return;
        }
        const uint32_t restored_force = undo_.force_id;
        const uint8_t restored_color = undo_.prev_color;
        undo_.valid = false;
        selected_force_id_ = restored_force;
        status_text_ = std::string(u8"已撤销修改，恢复势力 #") + std::to_string(restored_force) +
                       u8" 为颜色 #" + color_tag(restored_color) + u8"。";
        refresh_data();
        update_controls();
    }

    static void SAN14_MOD_CALL on_force_click(void* ctx, San14ControlHandle c) {
        static_cast<ForceColorDialog*>(ctx)->handle_select_force(c);
    }
    static void SAN14_MOD_CALL on_palette_click(void* ctx, San14ControlHandle c) {
        static_cast<ForceColorDialog*>(ctx)->handle_pick_color(c);
    }
    static void SAN14_MOD_CALL on_prev_page(void* ctx, San14ControlHandle) {
        auto* self = static_cast<ForceColorDialog*>(ctx);
        if (self->force_page_ > 0) --self->force_page_;
        self->update_controls();
    }
    static void SAN14_MOD_CALL on_next_page(void* ctx, San14ControlHandle) {
        auto* self = static_cast<ForceColorDialog*>(ctx);
        const size_t total_pages = self->forces_.empty() ? 1 : (self->forces_.size() + kForcesPerPage - 1) / kForcesPerPage;
        if (self->force_page_ + 1 < total_pages) ++self->force_page_;
        self->update_controls();
    }
    static void SAN14_MOD_CALL on_undo_click(void* ctx, San14ControlHandle) {
        static_cast<ForceColorDialog*>(ctx)->handle_undo();
    }
    static void SAN14_MOD_CALL on_refresh_click(void* ctx, San14ControlHandle) {
        auto* self = static_cast<ForceColorDialog*>(ctx);
        self->status_text_.clear();
        self->refresh_data();
        self->update_controls();
    }
    static void SAN14_MOD_CALL on_close_click(void* ctx, San14ControlHandle) {
        auto* self = static_cast<ForceColorDialog*>(ctx);
        if (self->host_ && self->panel_) {
            const auto h = self->panel_;
            self->clear_handles();
            self->host_->ui.close_panel(self->host_->host_context, h);
        }
    }
};

}  // namespace enhanced_settings_mod
