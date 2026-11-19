#pragma once

#include "san14_host.h"
#include "san14_officer_edit.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace enhanced_settings_mod {

// A stable modal panel: search, selection and application update existing
// controls, so callbacks never destroy their own native panel or userdata.
class OfficerStatsDialog {
public:
    static bool supported(const San14HostApi* h) {
        return h && h->struct_size >= offsetof(San14HostApi, officer_edit) + sizeof(h->officer_edit) &&
            h->officer_edit && h->officer_edit->struct_size >= sizeof(San14OfficerEditApi) &&
            h->officer_edit->read && h->officer_edit->apply && h->officer_edit->restore &&
            h->roster.struct_size >= sizeof(San14RosterApi) && h->roster.capture &&
            h->ui.create_panel && h->ui.add_label && h->ui.add_button && h->ui.set_text && h->ui.show_panel && h->ui.close_panel &&
            h->ui_extended.struct_size >= offsetof(San14UiExtendedApi, add_text_input) + sizeof(h->ui_extended.add_text_input) &&
            h->ui_extended.add_text_input && h->ui_style &&
            h->ui_style->struct_size >= offsetof(San14UiStyleApi, set_enabled) + sizeof(h->ui_style->set_enabled) &&
            (h->ui_style->capabilities & SAN14_UI_STYLE_EXPERT) &&
            (h->ui_style->capabilities & SAN14_UI_STYLE_ENABLED) && h->ui_style->set_enabled;
    }
    void bind(const San14HostApi* h) { host_ = h; }
    void reset() {
        if (host_ && panel_) host_->ui.close_panel(host_->host_context, panel_);
        panel_ = 0; rows_ = {}; stat_inputs_ = {}; status_label_ = selected_label_ = page_label_ = 0;
        apply_button_ = undo_button_ = previous_ = next_ = 0; search_input_ = 0;
        officers_.clear(); filtered_.clear(); loaded_ = {}; undo_before_ = {}; undo_after_ = {};
        undo_valid_ = false; search_.clear(); page_ = 0; texts_ = {}; list_generation_ = 0;
    }
    San14Result open() {
        if (!supported(host_)) return SAN14_E_UNSUPPORTED;
        reset();
        auto result = capture();
        if (result != SAN14_OK) return result;
        San14PanelSpec spec{sizeof(spec), sv(u8"武将基础能力"), 1120, 820, 0};
        result = host_->ui.create_panel(host_->host_context, &spec, &panel_);
        if (result != SAN14_OK) return result;
        try {
            label("stats.search.label", u8"按姓名或编号搜索：", 32, 48, 260, 38);
            San14ControlSpec input{sizeof(input), sv("stats.search"), sv(""), 280, 48, 280, 40, 0};
            check(host_->ui_extended.add_text_input(host_->host_context, panel_, &input, 128, text_changed, this, &search_input_));
            button("stats.search.go", u8"搜索", 570, 48, 130, 40, search_clicked);
            selected_label_ = label("stats.selected", u8"请先选择武将", 600, 115, 470, 44);
            for (size_t i = 0; i < rows_.size(); ++i)
                rows_[i].control = button("stats.row." + std::to_string(i), "", 32, 116 + int(i) * 58, 510, 48, select_clicked);
            const char* names[] = {u8"统率", u8"武力", u8"智力", u8"政治", u8"魅力"};
            for (size_t i = 0; i < stat_inputs_.size(); ++i) {
                label("stats.label." + std::to_string(i), names[i], 620, 190 + int(i) * 70, 150, 42);
                const auto id = "stats.value." + std::to_string(i);
                San14ControlSpec field{sizeof(field), sv(id), sv(""), 800, 190 + int(i) * 70, 230, 42, 0};
                check(host_->ui_extended.add_text_input(host_->host_context, panel_, &field, 3, text_changed, this, &stat_inputs_[i]));
            }
            previous_ = button("stats.previous", u8"上一页", 40, 602, 160, 44, previous_clicked);
            page_label_ = label("stats.pages", "", 222, 608, 140, 34);
            next_ = button("stats.next", u8"下一页", 380, 602, 160, 44, next_clicked);
            status_label_ = label("stats.status", u8"第一版支持五项基础能力均为1–100的在城未出阵武将；修改项同时同步经验。", 32, 660, 1040, 64);
            apply_button_ = button("stats.apply", u8"应用", 610, 584, 190, 46, apply_clicked);
            undo_button_ = button("stats.undo", u8"撤销上次修改", 830, 584, 230, 46, undo_clicked);
            button("stats.refresh", u8"刷新名单与数值", 230, 748, 280, 44, refresh_clicked);
            button("stats.close", u8"返回", 670, 748, 210, 44, close_clicked);
            filter(); update_rows(); update_values();
            check(host_->ui.show_panel(host_->host_context, panel_));
            return SAN14_OK;
        } catch (San14Result r) { reset(); return r; }
        catch (...) { reset(); return SAN14_E_PLUGIN_FAILED; }
    }
    San14PanelHandle panel() const { return panel_; }

private:
    struct Officer { uint32_t id; std::string name; };
    struct Row { San14ControlHandle control = 0; uint32_t id = 0; };
    const San14HostApi* host_ = nullptr;
    San14PanelHandle panel_ = 0;
    San14ControlHandle search_input_ = 0, selected_label_ = 0, status_label_ = 0, page_label_ = 0;
    San14ControlHandle apply_button_ = 0, undo_button_ = 0, previous_ = 0, next_ = 0;
    std::array<Row, 8> rows_{};
    std::array<San14ControlHandle, 5> stat_inputs_{};
    std::array<std::string, 5> texts_{};
    std::vector<Officer> officers_, filtered_;
    std::string search_;
    size_t page_ = 0;
    uint64_t list_generation_ = 0;
    San14OfficerEditState loaded_{}, undo_before_{}, undo_after_{};
    uint32_t undo_mask_ = 0;
    bool undo_valid_ = false;

    static San14StringView sv(const std::string& text) { return {text.data(), static_cast<uint32_t>(text.size())}; }
    static San14StringView sv(const char* text) { return {text, static_cast<uint32_t>(std::strlen(text))}; }
    static void check(San14Result result) { if (result != SAN14_OK) throw result; }
    void enabled(San14ControlHandle c, bool value) { check(host_->ui_style->set_enabled(host_->host_context, c, value ? 1 : 0)); }
    void text(San14ControlHandle c, const std::string& value) { check(host_->ui.set_text(host_->host_context, c, sv(value))); }
    San14ControlHandle label(const std::string& id, const std::string& value, int x, int y, int w, int h) {
        San14ControlSpec spec{sizeof(spec), sv(id), sv(value), x, y, w, h, 0}; San14ControlHandle control = 0;
        check(host_->ui.add_label(host_->host_context, panel_, &spec, &control)); return control;
    }
    San14ControlHandle button(const std::string& id, const std::string& value, int x, int y, int w, int h, San14ControlCallback callback) {
        San14ControlSpec spec{sizeof(spec), sv(id), sv(value), x, y, w, h, SAN14_CONTROL_TAB}; San14ControlHandle control = 0;
        check(host_->ui.add_button(host_->host_context, panel_, &spec, callback, this, &control)); return control;
    }
    void status(const std::string& value) { if (panel_ && status_label_) text(status_label_, value); }
    void failure(San14Result result) {
        if (result == SAN14_E_STALE_GENERATION) status(u8"战局已切换或重新读档，请刷新名单后重新选择武将。");
        else if (result == SAN14_E_BUSY) status(u8"当前人物状态或数值已变化，未执行修改。请归城或刷新后重试。");
        else if (result == SAN14_E_NOT_FOUND) status(u8"当前战局已找不到该武将，请刷新名单。");
        else if (result == SAN14_E_UNSUPPORTED) status(u8"当前版本暂不支持武将基础能力编辑。");
        else status(u8"操作未完成（" + std::to_string(result) + u8"），请刷新并检查当前数值。");
    }
    San14Result capture() {
        San14RosterInfo info{sizeof(info)}; uint32_t count = 0;
        auto result = host_->roster.capture(host_->host_context, &info, nullptr, sizeof(San14RosterOfficer), 0, &count);
        if (result != SAN14_OK && result != SAN14_E_BUFFER_TOO_SMALL) return result;
        if (!count || count > 6000) return SAN14_E_NOT_FOUND;
        std::vector<San14RosterOfficer> values(count); for (auto& row : values) row.struct_size = sizeof(row);
        uint32_t actual = 0; result = host_->roster.capture(host_->host_context, &info, values.data(), sizeof(San14RosterOfficer), count, &actual);
        if (result != SAN14_OK) return result; if (actual > count) return SAN14_E_STALE_GENERATION;
        std::vector<Officer> next;
        for (uint32_t i = 0; i < actual; ++i) {
            const auto& row = values[i]; const auto* end = static_cast<const char*>(std::memchr(row.name_utf8, 0, sizeof(row.name_utf8)));
            if (row.officer_id && row.officer_id <= 6000 && end && end != row.name_utf8)
                next.push_back({row.officer_id, std::string(row.name_utf8, end)});
        }
        if (next.empty()) return SAN14_E_NOT_FOUND;
        San14OfficerEditState probe{sizeof(probe)};
        result = host_->officer_edit->read(host_->host_context, next.front().id, &probe);
        if (result != SAN14_OK) return result;
        if (!probe.generation) return SAN14_E_BUSY;
        list_generation_ = probe.generation; officers_ = std::move(next); return SAN14_OK;
    }
    void filter() {
        filtered_.clear();
        for (const auto& row : officers_) if (search_.empty() || row.name.find(search_) != std::string::npos || std::to_string(row.id).find(search_) != std::string::npos)
            filtered_.push_back(row);
        page_ = 0;
    }
    void update_rows() {
        const auto first = page_ * rows_.size();
        for (size_t i = 0; i < rows_.size(); ++i) {
            const bool present = first + i < filtered_.size(); rows_[i].id = present ? filtered_[first + i].id : 0;
            text(rows_[i].control, present ? std::to_string(filtered_[first + i].id) + " " + filtered_[first + i].name : "");
            enabled(rows_[i].control, present);
        }
        const auto pages = (filtered_.size() + rows_.size() - 1) / rows_.size();
        text(page_label_, pages ? std::to_string(page_ + 1) + " / " + std::to_string(pages) : u8"没有匹配项");
        enabled(previous_, page_ > 0); enabled(next_, first + rows_.size() < filtered_.size());
    }
    void update_values() {
        const auto found = std::find_if(officers_.begin(), officers_.end(), [&](const Officer& o) { return o.id == loaded_.officer_id; });
        text(selected_label_, loaded_.officer_id ? (found == officers_.end() ? std::to_string(loaded_.officer_id) : found->name) + u8"・基础能力" : u8"请先选择武将");
        for (size_t i = 0; i < texts_.size(); ++i) { texts_[i] = loaded_.officer_id ? std::to_string(loaded_.base_stats[i]) : ""; text(stat_inputs_[i], texts_[i]); }
        enabled(apply_button_, loaded_.officer_id && (loaded_.flags & SAN14_OFFICER_EDIT_CAN_APPLY));
        enabled(undo_button_, undo_valid_);
    }
    void select(uint32_t id) {
        if (!id) return;
        San14OfficerEditState value{sizeof(value)}; const auto result = host_->officer_edit->read(host_->host_context, id, &value);
        if (result != SAN14_OK) { failure(result); return; }
        if (value.officer_id != id || value.generation != list_generation_) { failure(SAN14_E_STALE_GENERATION); return; }
        loaded_ = value; undo_valid_ = false; update_values();
        status((loaded_.flags & SAN14_OFFICER_EDIT_CAN_APPLY) ? u8"修改基础能力1–100，点击应用后写入当前战局；请正常保存游戏。" : u8"该武将目前只读。第一版支持五项基础能力均为1–100的在城未出阵武将。");
    }
    static bool parse_value(const std::string& text, uint32_t& out) {
        if (text.empty() || text.size() > 3) return false; uint32_t value = 0;
        for (const char c : text) { if (c < '0' || c > '9') return false; value = value * 10 + static_cast<unsigned>(c - '0'); }
        out = value; return true;
    }
    void apply() {
        if (!loaded_.officer_id || !(loaded_.flags & SAN14_OFFICER_EDIT_CAN_APPLY)) { failure(SAN14_E_BUSY); return; }
        San14OfficerStatEdit edit{}; edit.struct_size = sizeof(edit); edit.officer_id = loaded_.officer_id; edit.generation = loaded_.generation;
        for (size_t i = 0; i < 5; ++i) {
            uint32_t value = 0;
            if (!parse_value(texts_[i], value)) { status(u8"请输入1–100的整数，未执行修改。"); return; }
            edit.expected_stats[i] = loaded_.base_stats[i]; edit.expected_experience[i] = loaded_.experience[i]; edit.base_stats[i] = value;
            if (value != loaded_.base_stats[i]) {
                if (value < 1 || value > 100) { status(u8"修改值须为1–100的整数，未执行修改。"); return; }
                edit.field_mask |= 1u << i;
            }
        }
        if (!edit.field_mask) { status(u8"数值未变，无需应用。"); return; }
        San14OfficerEditState after{sizeof(after)}; const auto before = loaded_;
        const auto result = host_->officer_edit->apply(host_->host_context, &edit, &after);
        if (result != SAN14_OK) { failure(result); return; }
        undo_before_ = before; undo_after_ = after; undo_mask_ = edit.field_mask; undo_valid_ = true; loaded_ = after;
        update_values(); status(u8"已应用基础能力并同步经验。请正常保存游戏；统计中的净成长会包含这次编辑。");
    }
    void undo() {
        if (!undo_valid_) return;
        San14OfficerStatRestore restore{}; restore.struct_size = sizeof(restore); restore.officer_id = undo_after_.officer_id;
        restore.generation = undo_after_.generation; restore.field_mask = undo_mask_;
        for (size_t i = 0; i < 5; ++i) {
            restore.expected_stats[i] = undo_after_.base_stats[i]; restore.expected_experience[i] = undo_after_.experience[i];
            restore.base_stats[i] = undo_before_.base_stats[i]; restore.experience[i] = undo_before_.experience[i];
        }
        San14OfficerEditState after{sizeof(after)}; const auto result = host_->officer_edit->restore(host_->host_context, &restore, &after);
        if (result != SAN14_OK) { failure(result); return; }
        loaded_ = after; undo_valid_ = false; update_values(); status(u8"已撤销上次修改，恢复对应基础能力和原有经验。");
    }
    template<class F> static void invoke(void* context, F fn) noexcept {
        auto* self = static_cast<OfficerStatsDialog*>(context);
        try { fn(*self); } catch (...) { try { self->failure(SAN14_E_PLUGIN_FAILED); } catch (...) {} }
    }
    static void SAN14_MOD_CALL text_changed(void* context, San14ControlHandle control, San14StringView value) {
        invoke(context, [&](OfficerStatsDialog& self) {
            const auto text = value.data ? std::string(value.data, value.length) : std::string{};
            if (control == self.search_input_) self.search_ = text;
            else for (size_t i = 0; i < self.stat_inputs_.size(); ++i) if (control == self.stat_inputs_[i]) self.texts_[i] = text;
        });
    }
    static void SAN14_MOD_CALL select_clicked(void* c, San14ControlHandle control) { invoke(c, [&](OfficerStatsDialog& self) { for (const auto& row : self.rows_) if (row.control == control) { self.select(row.id); return; } }); }
    static void SAN14_MOD_CALL search_clicked(void* c, San14ControlHandle) { invoke(c, [](OfficerStatsDialog& self) { self.filter(); self.update_rows(); }); }
    static void SAN14_MOD_CALL previous_clicked(void* c, San14ControlHandle) { invoke(c, [](OfficerStatsDialog& self) { if (self.page_) --self.page_; self.update_rows(); }); }
    static void SAN14_MOD_CALL next_clicked(void* c, San14ControlHandle) { invoke(c, [](OfficerStatsDialog& self) { if ((self.page_ + 1) * self.rows_.size() < self.filtered_.size()) ++self.page_; self.update_rows(); }); }
    static void SAN14_MOD_CALL apply_clicked(void* c, San14ControlHandle) { invoke(c, [](OfficerStatsDialog& self) { self.apply(); }); }
    static void SAN14_MOD_CALL undo_clicked(void* c, San14ControlHandle) { invoke(c, [](OfficerStatsDialog& self) { self.undo(); }); }
    static void SAN14_MOD_CALL refresh_clicked(void* c, San14ControlHandle) {
        invoke(c, [](OfficerStatsDialog& self) {
            const auto result = self.capture(); if (result != SAN14_OK) { self.failure(result); return; }
            self.loaded_ = {}; self.undo_valid_ = false; self.filter(); self.update_rows(); self.update_values();
            self.status(u8"名单已刷新，未应用的输入和撤销记录已清除，请重新选择武将。");
        });
    }
    static void SAN14_MOD_CALL close_clicked(void* c, San14ControlHandle) { invoke(c, [](OfficerStatsDialog& self) { self.reset(); }); }
};

} // namespace enhanced_settings_mod
