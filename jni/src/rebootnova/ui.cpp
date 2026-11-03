#include "rebootnova/ui.h"
#include "rebootnova/build_variant.h"
#include "rebootnova/slider_step.h"
#include <cstdio>

#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cfloat>
#include <string>

namespace rebootnova {
namespace {

constexpr ImVec4 kPanel{0.086F, 0.094F, 0.122F, 0.96F};
constexpr ImVec4 kRail{0.067F, 0.075F, 0.102F, 0.97F};
constexpr ImVec4 kModules{0.098F, 0.106F, 0.133F, 0.97F};
constexpr ImVec4 kDetail{0.082F, 0.090F, 0.118F, 0.97F};
constexpr ImVec4 kCard{0.125F, 0.137F, 0.173F, 0.86F};
constexpr ImVec4 kText{0.945F, 0.941F, 0.961F, 1.0F};
constexpr ImVec4 kMuted{0.541F, 0.553F, 0.596F, 1.0F};

ImVec4 accent(const Settings& settings, float alpha = 1.0F) {
    return {settings.accent[0], settings.accent[1], settings.accent[2], alpha};
}

ImU32 accent_u32(const Settings& settings, float alpha = 1.0F) {
    return ImGui::ColorConvertFloat4ToU32(accent(settings, alpha));
}

void clamp_current_window() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 clamped_pos{
        std::clamp(position.x, 0.0F, std::max(0.0F, display.x - size.x)),
        std::clamp(position.y, 0.0F, std::max(0.0F, display.y - size.y))
    };
    if (clamped_pos.x != position.x || clamped_pos.y != position.y)
        ImGui::SetWindowPos(clamped_pos, ImGuiCond_Always);
}

void draw_hamburger(ImDrawList* draw, const ImVec2 center, float radius, ImU32 color) {
    const float half = radius * 0.62F;
    const float gap = radius * 0.43F;
    for (int i = -1; i <= 1; ++i)
        draw->AddLine({center.x - half, center.y + gap * static_cast<float>(i)},
                      {center.x + half, center.y + gap * static_cast<float>(i)}, color, 2.4F);
}



void section_header(const Settings& settings, const char* label, const char* subtitle) {
    ImGui::Spacing();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float font = ImGui::GetFontSize();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled({origin.x, origin.y + font * 0.12F}, {origin.x + 4.0F, origin.y + font * 0.92F},
                        accent_u32(settings, 0.92F), 2.0F);
    ImGui::Indent(11.0F);
    ImGui::PushStyleColor(ImGuiCol_Text, accent(settings));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if (subtitle && subtitle[0]) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextUnformatted(subtitle);
        ImGui::PopStyleColor();
    }
    ImGui::Unindent(11.0F);
}

float fitted_font_size(const char* text, float base_size, float min_size, float max_width) {
    if (text == nullptr || max_width <= 0.0F) return min_size;
    float size = base_size;
    while (size > min_size && ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0F, text).x > max_width)
        size -= 1.0F;
    return size;
}


void begin_card(const char* id) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.0F, 10.0F});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kCard);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0F, 1.0F, 1.0F, 0.055F));
    ImGui::BeginChild(id, {0.0F, 0.0F},
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void end_card() {
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ImGui::Dummy({0.0F, 4.0F});
}

void text_wrapped_disabled(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("%s", text == nullptr ? "" : text);
    ImGui::PopStyleColor();
}

bool full_width_combo(const char* id, const char* label, int* value, const char* items) {
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    return ImGui::Combo(id, value, items);
}

// Keep the slider full-width; large step buttons use their own row on phones.
int slider_step_buttons(double value,double minimum,double maximum,double step) {
    const float available=std::max(1.F,ImGui::GetContentRegionAvail().x);
    const float gap=std::min(ImGui::GetStyle().ItemSpacing.x,available*.15F);
    const float width=std::max(1.F,(available-gap)*.5F);
    const float height=std::max(ImGui::GetFrameHeight(),ImGui::GetFontSize()*1.65F);
    char minus[40],plus[40];
    std::snprintf(minus,sizeof(minus),"- %g###decrement",step);
    std::snprintf(plus,sizeof(plus),"+ %g###increment",step);
    int direction=0;
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat,true);
    ImGui::BeginDisabled(value<=minimum);
    if(ImGui::Button(minus,{width,height}))direction=-1;
    ImGui::EndDisabled();
    ImGui::SameLine(0,gap);
    ImGui::BeginDisabled(value>=maximum);
    if(ImGui::Button(plus,{width,height}))direction=1;
    ImGui::EndDisabled();
    ImGui::PopItemFlag();
    return direction;
}

bool full_width_slider_float(const char* id, const char* label, float* value,
                             float minimum, float maximum, const char* format) {
    ImGui::PushID(id);
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    bool changed=ImGui::SliderFloat("##slider", value, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp);
    const double step=slider_step::precision(format);
    const float before=*value;
    const int direction=slider_step_buttons(*value,minimum,maximum,step);
    if(direction)*value=slider_step::adjust(*value,minimum,maximum,step,direction);
    changed |= *value!=before;
    ImGui::PopID();
    return changed;
}

bool full_width_slider_int(const char* id, const char* label, int* value,
                           int minimum, int maximum) {
    ImGui::PushID(id);
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    bool changed=ImGui::SliderInt("##slider", value, minimum, maximum, "%d", ImGuiSliderFlags_AlwaysClamp);
    const int before=*value;
    const int direction=slider_step_buttons(*value,minimum,maximum,1);
    if(direction)*value=slider_step::adjust(*value,minimum,maximum,direction);
    changed |= *value!=before;
    ImGui::PopID();
    return changed;
}

bool full_width_slider_percent(const char* id,const char* label,float* normalized) {
    float percent=*normalized*100.F;
    const bool changed=full_width_slider_float(id,label,&percent,0.F,100.F,"%.0f%%");
    if(changed)*normalized=percent*.01F;
    return changed;
}

bool full_width_line_style(const char* id,const char* label,Language language,float* thickness,std::array<float,4>& color){
    ImGui::PushID(id);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    bool changed=ImGui::ColorEdit4("##color",color.data(),ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoAlpha);
    changed |= full_width_slider_float("##thickness",tr(language,"线条粗细","Line thickness"),thickness,.5F,8.F,"%.1f px");
    changed |= full_width_slider_percent("##opacity",tr(language,"不透明�?","Opacity"),&color[3]);
    ImGui::PopID();
    return changed;
}

bool bool_mode_selector(const char* id, const char* label, const char* off_label,
                        const char* on_label, bool& value, const Settings& settings) {
    ImGui::TextUnformatted(label);
    ImGui::PushID(id);
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float width = std::max(1.0F, (ImGui::GetContentRegionAvail().x - gap) * 0.5F);
    const auto button = [&](const char* text, bool selected) {
        ImGui::PushStyleColor(ImGuiCol_Button,
            selected ? accent(settings, 0.72F) : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        const bool pressed = ImGui::Button(text, {width, 0.0F});
        ImGui::PopStyleColor();
        return pressed;
    };
    const bool before = value;
    if (button(off_label, !value)) value = false;
    ImGui::SameLine(0.0F, gap);
    if (button(on_label, value)) value = true;
    ImGui::PopID();
    return value != before;
}

const char* category_name(Language language, int category) {
    if (category == 1) return "ESP";
    return tr(language, "设置", "SETTINGS");
}

const char* module_title(Language language, int category, int selected) {
    if (language == Language::Chinese) {
        if (category == 1) return localized_zh("Player Boxes");
        const char* titles[] = {localized_zh("Runtime Status"), localized_zh("Configuration"), localized_zh("Exit")};
        return titles[std::clamp(selected, 0, 2)];
    }
    if (category == 1) {
        return tr(language, "玩家方框", "Player Boxes");
    }
    const char* zh[] = {"运行状�?","配置", "退�?"};
    const char* en[] = {"Runtime Status", "Configuration", "Exit"};
    return language == Language::Chinese ? zh[std::clamp(selected, 0, 2)] : en[std::clamp(selected, 0, 2)];
}

}  // namespace

Ui::~Ui() = default;

void Ui::apply_theme() {
    if (settings_.theme == 1) ImGui::StyleColorsLight();
    else if (settings_.theme == 2) ImGui::StyleColorsClassic();
    else ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    if (settings_.theme == 0) {
        style.Colors[ImGuiCol_Text] = kText;
        style.Colors[ImGuiCol_TextDisabled] = kMuted;
        style.Colors[ImGuiCol_WindowBg] = kPanel;
        style.Colors[ImGuiCol_ChildBg] = kDetail;
        style.Colors[ImGuiCol_PopupBg] = {0.075F, 0.082F, 0.106F, 0.98F};
        style.Colors[ImGuiCol_Border] = {1.0F, 1.0F, 1.0F, 0.08F};
        style.Colors[ImGuiCol_FrameBg] = {0.188F, 0.200F, 0.235F, 1.0F};
        style.Colors[ImGuiCol_FrameBgHovered] = accent(settings_, 0.24F);
        style.Colors[ImGuiCol_FrameBgActive] = accent(settings_, 0.34F);
        style.Colors[ImGuiCol_Button] = {0.173F, 0.184F, 0.220F, 1.0F};
        style.Colors[ImGuiCol_ButtonHovered] = accent(settings_, 0.34F);
        style.Colors[ImGuiCol_Header] = accent(settings_, 0.22F);
        style.Colors[ImGuiCol_HeaderHovered] = accent(settings_, 0.32F);
        style.Colors[ImGuiCol_ResizeGrip] = accent(settings_, 0.40F);
        style.Colors[ImGuiCol_ResizeGripHovered] = accent(settings_, 0.78F);
        style.Colors[ImGuiCol_ResizeGripActive] = accent(settings_);
        style.Colors[ImGuiCol_Separator] = {1.0F, 1.0F, 1.0F, 0.07F};
        style.Colors[ImGuiCol_ScrollbarBg] = {0.055F, 0.059F, 0.075F, 1.0F};
        style.Colors[ImGuiCol_ScrollbarGrab] = {0.35F, 0.37F, 0.43F, 1.0F};
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = accent(settings_, 0.72F);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = accent(settings_, 0.94F);
        style.WindowRounding = 12.0F;
        style.ChildRounding = 0.0F;
        style.FrameRounding = 8.0F;
        style.PopupRounding = 8.0F;
        style.ScrollbarRounding = 12.0F;
        style.GrabRounding = 10.0F;
    }
    style.Colors[ImGuiCol_CheckMark] = accent(settings_);
    style.Colors[ImGuiCol_SliderGrab] = accent(settings_);
    style.Colors[ImGuiCol_SliderGrabActive] = accent(settings_);
    style.Colors[ImGuiCol_ButtonActive] = accent(settings_);
}

void Ui::status_tab(bool touch_ready, const char* renderer_name) {
    const auto language = settings_.language;
    const auto& frame = features_.frame();
    ImGui::Text("%s: %s", tr(language, "渲染�?","Renderer"), renderer_name);
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("%s: %s", tr(language, "触控", "Touch"), touch_ready ? tr(language, "就绪", "Ready") : tr(language, "不可�?","Unavailable"));
    ImGui::Text("PID: %d", static_cast<int>(frame.pid));
    ImGui::Text("Module: %s @ 0x%llX", frame.module_name.empty() ? "-" : frame.module_name.c_str(),
                static_cast<unsigned long long>(frame.module_base));
    ImGui::Text("%s: %s", tr(language, "游戏状�?","Game status"), frame.local.valid ? tr(language, "已连�?","Attached") : tr(language, "等待进程", "Waiting"));
    ImGui::Text("%s: %zu", tr(language, "实体�?","Entities"), frame.entities.size());
    ImGui::Text("Build / CL: V27 %s candidate / %s", build_variant::name, profile_.build.c_str());
}


void Ui::config_tab() {
    const auto language = settings_.language;
    if (ImGui::Button(tr(language, "保存配置", "Save Config")))
        status_ = config_.save(settings_, &status_) ? tr(language, "配置已保�?","Config saved") : status_;
    ImGui::SameLine();
    if (ImGui::Button(tr(language, "加载配置", "Load Config"))) {
        const bool old_record_visible = settings_.record_visible;
        status_ = config_.load(settings_, &status_) ? tr(language, "配置已加�?","Config loaded") : status_;
        if (old_record_visible != settings_.record_visible) surface_recreate_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(language, "恢复默认", "Reset Config"))) {
        const bool old_record_visible = settings_.record_visible;
        status_ = config_.reset(settings_, &status_) ? tr(language, "已恢复默�?","Defaults restored") : status_;
        if (old_record_visible != settings_.record_visible) surface_recreate_ = true;
    }
    ImGui::TextWrapped("%s: %s", tr(language, "路径", "Path"), config_.path().c_str());
    if (!status_.empty()) ImGui::TextWrapped("%s", status_.c_str());
}

void Ui::esp_detail() {
    const auto language = settings_.language;
    text_wrapped_disabled(tr(language, "这个版本只画玩家方框�?","This build draws player boxes only."));
    begin_card("##esp_boxes");
    ImGui::Checkbox(tr(language, "玩家方框", "Player Boxes"), &settings_.boxes);
    ImGui::ColorEdit3(tr(language, "方框颜色", "Box Color"), settings_.accent.data(), ImGuiColorEditFlags_NoAlpha);
    end_card();
}










void Ui::render_header_bar() {
    const auto language = settings_.language;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float header_height = 66.0F * ui_scale_;
    const float gap = 10.0F * ui_scale_;
    const float control_height = 46.0F * ui_scale_;
    const float row_width = ImGui::GetContentRegionAvail().x;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + row_width, origin.y + header_height}, IM_COL32(31, 33, 42, 245));
    draw->AddLine({origin.x, origin.y + header_height}, {origin.x + row_width, origin.y + header_height},
                  accent_u32(settings_, 0.55F), 1.4F);

    const float control_padding_x = 10.0F * ui_scale_;
    const float frame_extra = control_padding_x * 2.0F;
    const char* minimize_label = tr(language, "\xE6\x9C\x80\xE5\xB0\x8F\xE5\x8C\x96", "Minimize");
    float zh_width = ImGui::CalcTextSize("\xE4\xB8\xAD\xE6\x96\x87").x + frame_extra;
    float en_width = ImGui::CalcTextSize("English").x + frame_extra;
    float minimize_width = ImGui::CalcTextSize(minimize_label).x + frame_extra;
    float controls_width = zh_width + en_width + minimize_width + gap * 2.0F + 16.0F * ui_scale_;
    // Keep the row on one line even on narrow windows: shrink the buttons instead of wrapping.
    const float room = std::max(1.0F, row_width - 70.0F * ui_scale_);
    if (controls_width > room) {
        const float ratio = room / controls_width;
        zh_width *= ratio;
        en_width *= ratio;
        minimize_width *= ratio;
        controls_width = zh_width + en_width + minimize_width + gap * 2.0F + 16.0F * ui_scale_;
    }
    const float drag_width = std::max(70.0F, row_width - controls_width);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {control_padding_x, 12.0F * ui_scale_});
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
    ImGui::InvisibleButton("##single_drag", {drag_width, header_height});
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0F)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        ImGui::SetWindowPos("##RebootNovaCompact", {expanded_x_ + delta.x, expanded_y_ + delta.y}, ImGuiCond_Always);
    }
    const float grip_x = origin.x + 16.0F * ui_scale_;
    const float grip_width = 16.0F * ui_scale_;
    for (int i = -1; i <= 1; ++i) {
        const float line_y = origin.y + header_height * 0.5F + static_cast<float>(i) * 7.0F * ui_scale_;
        draw->AddLine({grip_x, line_y}, {grip_x + grip_width, line_y}, accent_u32(settings_, 0.85F), 2.2F);
    }
    const char* title = tr(language, "\xE5\x8D\x95\xE9\x80\x8F\xE8\xA7\x86", "Single ESP");
    const float title_size = fitted_font_size(title, std::max(0.0F, drag_width - grip_width - 48.0F * ui_scale_),
                                              ImGui::GetFontSize() * 0.94F, ImGui::GetFontSize() * 0.56F);
    draw->AddText(ImGui::GetFont(), title_size,
                  {grip_x + grip_width + 12.0F * ui_scale_, origin.y + (header_height - title_size * 1.30F) * 0.5F},
                  IM_COL32(244, 242, 248, 255), title);

    const bool is_chinese = language == Language::Chinese;
    const ImVec4 idle{0.173F, 0.184F, 0.220F, 1.0F};
    const ImVec4 on_text{0.035F, 0.043F, 0.062F, 1.0F};
    ImGui::SameLine(0.0F, gap);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (header_height - control_height) * 0.5F);
    ImGui::PushStyleColor(ImGuiCol_Button, is_chinese ? accent(settings_, 0.92F) : idle);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, is_chinese ? accent(settings_, 0.92F) : accent(settings_, 0.34F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent(settings_));
    ImGui::PushStyleColor(ImGuiCol_Text, is_chinese ? on_text : kText);
    if (ImGui::Button("\xE4\xB8\xAD\xE6\x96\x87", {zh_width, control_height})) settings_.language = Language::Chinese;
    ImGui::PopStyleColor(4);

    ImGui::SameLine(0.0F, 1.0F);
    ImGui::PushStyleColor(ImGuiCol_Button, is_chinese ? idle : accent(settings_, 0.92F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, is_chinese ? accent(settings_, 0.34F) : accent(settings_, 0.92F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent(settings_));
    ImGui::PushStyleColor(ImGuiCol_Text, is_chinese ? kText : on_text);
    if (ImGui::Button("English", {en_width, control_height})) settings_.language = Language::English;
    ImGui::PopStyleColor(4);

    ImGui::SameLine(0.0F, gap);
    ImGui::PushStyleColor(ImGuiCol_Button, idle);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent(settings_, 0.34F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent(settings_));
    if (ImGui::Button(minimize_label, {minimize_width, control_height})) collapsed_ = true;
    ImGui::PopStyleColor(3);

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}




void Ui::render_single_page(bool* running, bool touch_ready, const char* renderer_name) {
    const auto language = settings_.language;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {18.0F, 15.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {10.0F, 9.0F});
    const float detail_scrollbar_size = std::clamp(64.0F * ui_scale_, 56.0F, 72.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, detail_scrollbar_size);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kDetail);
    ImGui::PushStyleColor(ImGuiCol_Text, kText);
    ImGui::BeginChild("##detail_body", {0.0F, 0.0F}, 0, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    section_header(settings_, tr(language, "\xE5\x8A\x9F\xE8\x83\xBD", "Features"),
                   tr(language, "\xE5\x8F\xAA\xE6\x98\xBE\xE7\xA4\xBA\xE6\x96\xB9\xE6\xA1\x86", "Boxes only"));
    esp_detail();

    section_header(settings_, tr(language, "\xE6\x98\xBE\xE7\xA4\xBA\xE4\xBF\xA1\xE6\x81\xAF", "Display Info"),
                   tr(language, "\xE8\xBF\x9B\xE7\xA8\x8B\xE4\xB8\x8E\xE6\xB8\xB2\xE6\x9F\x93\xE7\x8A\xB6\xE6\x80\x81",
                       "Process and rendering status"));
    status_tab(touch_ready, renderer_name);

    section_header(settings_, tr(language, "\xE9\x85\x8D\xE7\xBD\xAE", "Configuration"),
                   tr(language, "\xE4\xBF\x9D\xE5\xAD\x98\xE3\x80\x81\xE5\x8A\xA0\xE8\xBD\xBD\xE3\x80\x81\xE6\x81\xA2\xE5\xA4\x8D",
                       "Save, load, reset"));
    text_wrapped_disabled(tr(language,
                             "\xE4\xBF\x9D\xE5\xAD\x98\xE3\x80\x81\xE5\x8A\xA0\xE8\xBD\xBD\xE6\x88\x96\xE6\x81\xA2\xE5\xA4\x8D\xE9\x80\x8F\xE8\xA7\x86\xE8\xAE\xBE\xE7\xBD\xAE\xE3\x80\x82",
                             "Save, load or restore ESP settings."));
    config_tab();

    ImGui::Spacing();
    if (ImGui::Button(tr(language, "\xE9\x80\x80\xE5\x87\xBA", "Exit"), {-1.0F, 64.0F})) *running = false;
    text_wrapped_disabled(tr(language,
                             "\xE9\x80\x80\xE5\x87\xBA\xE5\xB0\x86\xE5\x85\xB3\xE9\x97\xAD\xE6\x82\xAC\xE6\xB5\xAE\xE5\xB1\x82\xE3\x80\x82",
                             "Exit closes the overlay."));

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}


void Ui::render_collapsed_button() {
    const float size = 68.0F * ui_scale_;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    expanded_x_ = std::clamp(expanded_x_, 0.0F, std::max(0.0F, display.x - size));
    expanded_y_ = std::clamp(expanded_y_, 0.0F, std::max(0.0F, display.y - size));
    ImGui::SetNextWindowPos({expanded_x_, expanded_y_}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({size, size}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0F);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.098F, 0.106F, 0.141F, 0.94F));
    ImGui::PushStyleColor(ImGuiCol_Border, accent(settings_, 0.32F));
    ImGui::Begin("##RebootNovaFab", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##open_compact_overlay", {size, size})) {
        collapsed_ = false;
        restore_expanded_position_ = true;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered()) draw->AddRectFilled(p, {p.x + size, p.y + size}, accent_u32(settings_, 0.24F), 14.0F);
    draw_hamburger(draw, {p.x + size * 0.5F, p.y + size * 0.5F}, 17.0F * ui_scale_, IM_COL32(244, 241, 249, 255));
    draw->AddCircleFilled({p.x + size - 10.0F, p.y + size - 10.0F}, 4.0F, accent_u32(settings_), 16);
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

bool Ui::render(bool* running, bool touch_ready, const char* renderer_name) {
    apply_theme();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ui_scale_ = std::clamp(display.y / 1080.0F, 0.76F, 1.12F);

    if (!has_expanded_position_) {
        expanded_x_ = std::max(8.0F, display.x * 0.085F);
        expanded_y_ = std::max(8.0F, display.y * 0.11F);
        has_expanded_position_ = true;
    }
    if (collapsed_) {
        render_collapsed_button();
        return true;
    }

    const float min_width = std::min(330.0F, std::max(260.0F, display.x - 24.0F));
    const float min_height = std::min(280.0F, std::max(220.0F, display.y - 24.0F));
    const float max_width = std::max(min_width, display.x - 12.0F);
    const float max_height = std::max(min_height, display.y - 12.0F);
    if (!has_expanded_size_) {
        expanded_width_ = std::clamp(std::min(470.0F * ui_scale_, display.x - 24.0F), min_width, max_width);
        expanded_height_ = std::clamp(std::min(640.0F * ui_scale_, display.y - 24.0F), min_height, max_height);
        has_expanded_size_ = true;
    }
    expanded_width_ = std::clamp(expanded_width_, min_width, max_width);
    expanded_height_ = std::clamp(expanded_height_, min_height, max_height);
    ImGui::SetNextWindowSize({expanded_width_, expanded_height_}, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints({min_width, min_height}, {max_width, max_height});
    if (restore_expanded_position_) {
        ImGui::SetNextWindowPos({expanded_x_, expanded_y_}, ImGuiCond_Always);
        restore_expanded_position_ = false;
    } else {
        ImGui::SetNextWindowPos({expanded_x_, expanded_y_}, ImGuiCond_FirstUseEver);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0F);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0F, 1.0F, 1.0F, 0.08F));
    ImGui::Begin("##RebootNovaCompact", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    clamp_current_window();
    const ImVec2 current_position = ImGui::GetWindowPos();
    const ImVec2 current_size = ImGui::GetWindowSize();
    expanded_x_ = current_position.x;
    expanded_y_ = current_position.y;
    // Native ImGui resizing is processed during Begin(). Persist the resulting
    // size immediately so SetNextWindowSize() does not restore the old size on
    // the next frame after the finger is released.
    if (!resizing_) {
        expanded_width_ = std::clamp(current_size.x, min_width, max_width);
        expanded_height_ = std::clamp(current_size.y, min_height, max_height);
    }

    render_header_bar();
    render_single_page(running, touch_ready, renderer_name);

    // A large touch-friendly bottom-right grip. Native ImGui resize remains enabled,
    // while this direct path makes the same gesture reliable on touch surfaces.
    const ImVec2 window_pos = ImGui::GetWindowPos();
    const ImVec2 window_size = ImGui::GetWindowSize();
    const float grip_size = 88.0F * ui_scale_;
    const ImVec2 grip_min{window_pos.x + window_size.x - grip_size, window_pos.y + window_size.y - grip_size};
    const ImVec2 grip_max{window_pos.x + window_size.x, window_pos.y + window_size.y};
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool grip_hovered = mouse.x >= grip_min.x && mouse.x <= grip_max.x && mouse.y >= grip_min.y && mouse.y <= grip_max.y;
    if (!resizing_ && grip_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive()) {
        resizing_ = true;
        resize_start_mouse_x_ = mouse.x;
        resize_start_mouse_y_ = mouse.y;
        resize_start_width_ = window_size.x;
        resize_start_height_ = window_size.y;
    }
    if (resizing_) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            expanded_width_ = std::clamp(resize_start_width_ + mouse.x - resize_start_mouse_x_, min_width, max_width);
            expanded_height_ = std::clamp(resize_start_height_ + mouse.y - resize_start_mouse_y_, min_height, max_height);
            ImGui::SetWindowSize("##RebootNovaCompact", {expanded_width_, expanded_height_}, ImGuiCond_Always);
        } else {
            resizing_ = false;
        }
    }
    ImDrawList* window_draw = ImGui::GetWindowDrawList();
    const ImU32 grip_color = resizing_ || grip_hovered ? accent_u32(settings_, 0.95F) : accent_u32(settings_, 0.58F);
    for (int i = 0; i < 3; ++i) {
        const float inset = 16.0F + static_cast<float>(i) * 12.0F;
        window_draw->AddLine({window_pos.x + window_size.x - inset, window_pos.y + window_size.y - 4.0F},
                             {window_pos.x + window_size.x - 4.0F, window_pos.y + window_size.y - inset}, grip_color, 2.0F);
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    return true;
}

}  // namespace rebootnova
