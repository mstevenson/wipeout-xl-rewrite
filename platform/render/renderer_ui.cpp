#include "renderer_ui.h"
#include "imgui.h"

static bool Check(const char *label, int *value)
{
    bool enabled = *value != 0;

    if (!ImGui::Checkbox(label, &enabled)) {
        return false;
    }
    *value = enabled;
    return true;
}

static bool Ps1Settings(Ps1RenderSettings *s)
{
    bool changed = Check("Quantize vertex positions", &s->quantizeVertices);
    changed |= Check("Affine texture mapping", &s->affineTextures);
    changed |= Check("5-bit colour precision", &s->colourPrecision);
    ImGui::BeginDisabled(!s->colourPrecision);
    changed |= Check("PS1 dithering", &s->dither);
    ImGui::EndDisabled();
    changed |= Check("Depth buffer (enhancement)", &s->depth);
    ImGui::TextDisabled("Default: ordering tables determine visibility.");
    return changed;
}

static bool PcSettings(PcRenderSettings *s)
{
    bool changed = Check("Bilinear texture filtering", &s->bilinear);
    changed |= Check("PC scenery and ship lighting", &s->lighting);
    changed |= Check("Depth buffer", &s->depth);
    return changed;
}

static bool N64Settings(N64RenderSettings *s)
{
    bool changed = Check("Three-point texture filtering", &s->threePoint);
    changed |= Check("RGB5 framebuffer precision", &s->colourPrecision);
    ImGui::BeginDisabled(!s->colourPrecision);
    changed |= Check("Magic-square dithering", &s->dither);
    changed |= Check("VI dedithering", &s->dedither);
    ImGui::EndDisabled();
    changed |= Check("VI divot filtering (approximate)", &s->divot);
    changed |= Check("Linear display resampling", &s->resample);
    ImGui::TextDisabled("Three-point filtering excludes 2D HUD and menus.");
    ImGui::TextWrapped("VI filtering approximates the N64 display and also processes the HUD.");
    return changed;
}

static bool ProfileSettings(RenderSettings *s, RendererId edit)
{
    bool changed = false;

    ImGui::PushID((int)edit);
    ImGui::SeparatorText(RenderGet(edit)->name);
    ImGui::PushTextWrapPos(0);
    switch (edit) {
    case RENDER_PC: changed |= PcSettings(&s->pc); break;
    case RENDER_N64: changed |= N64Settings(&s->n64); break;
    default: changed |= Ps1Settings(&s->ps1); break;
    }
    if (ImGui::Button("Restore renderer defaults")) {
        RenderDefaults(s, edit);
        changed = true;
    }
    ImGui::PopTextWrapPos();
    ImGui::PopID();
    return changed;
}

bool RenderSettingsUi(RenderSettings *s)
{
    const char *names[] = {"PlayStation", "PC"};
    RendererId base = s->selection == RENDER_PS1 || s->selection == RENDER_PC
        ? s->selection : s->automaticBase;
    bool n64Tracks = s->selection == RENDER_AUTO || s->selection == RENDER_N64;
    int choice = base == RENDER_PC;

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    bool changed = ImGui::Combo("Global renderer", &choice, names, 2);
    changed |= ImGui::Checkbox("Use N64 renderer for Wipeout 64 tracks", &n64Tracks);
    if (changed) {
        base = choice ? RENDER_PC : RENDER_PS1;
        /* Retain the existing INI format: Automatic is the track-specific N64 override. */
        s->automaticBase = base;
        s->selection = n64Tracks ? RENDER_AUTO : base;
    }
    ImGui::Text("Active renderer: %s", RenderGet(RenderCurrent()->id)->name);
    ImGui::TextDisabled("Changes apply to the next game frame.");

    /* Size from the longest control, so columns also fit when the UI font is scaled. */
    const ImGuiStyle &style = ImGui::GetStyle();
    float panelWidth = ImGui::CalcTextSize("VI divot filtering (approximate)").x +
        ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + style.CellPadding.x * 2;
    bool sideBySide = ImGui::GetContentRegionAvail().x >= panelWidth * 2;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings;
    if (sideBySide) flags |= ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("Renderer settings", sideBySide ? 2 : 1, flags)) {
        ImGui::TableNextColumn();
        changed |= ProfileSettings(s, base);
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!n64Tracks);
        changed |= ProfileSettings(s, RENDER_N64);
        ImGui::EndDisabled();
        ImGui::EndTable();
    }
    return changed;
}
