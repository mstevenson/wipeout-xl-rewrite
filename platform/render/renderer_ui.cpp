#include "renderer_ui.h"
#include "imgui.h"

static void Tooltip(const char *text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled) &&
        ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static bool Check(const char *label, int *value, const char *tooltip)
{
    bool enabled = *value != 0;
    bool changed = ImGui::Checkbox(label, &enabled);

    Tooltip(tooltip);
    if (!changed) {
        return false;
    }
    *value = enabled;
    return true;
}

static bool DisplaySettings(int *resample)
{
    return Check("Linear display resampling", resample,
        "Blends neighbouring pixels when scaling the final image to the window, softening the scene and HUD. "
        "Disable for sharp, nearest-neighbour scaling. Independent of texture filtering.");
}

static bool Ps1Settings(Ps1RenderSettings *s)
{
    bool changed = Check("PS1 vertex snapping", &s->quantizeVertices,
        "Snaps projected vertices to whole pixels in screen space, reproducing PS1-style geometry jitter. "
        "Disable for smoother movement.");
    changed |= Check("Affine texture mapping", &s->affineTextures,
        "Maps textures across polygons without perspective correction, producing PS1-style texture warping. "
        "Disable for perspective-correct textures.");
    changed |= Check("5-bit colour precision", &s->colourPrecision,
        "Reduces rendered colours to 32 levels per red, green and blue channel, producing PS1-style colour banding. "
        "Disable for finer colour gradients.");
    ImGui::BeginDisabled(!s->colourPrecision);
    changed |= Check("PS1 dithering", &s->dither,
        "Adds the PS1 dither pattern to polygons that request it, reducing visible colour banding at the cost of "
        "a grainy pattern. Requires 5-bit colour precision.");
    ImGui::EndDisabled();
    changed |= Check("Depth buffer (enhancement)", &s->depth,
        "Uses polygon depth to help nearer surfaces hide farther ones, reducing draw-order errors. "
        "Disable to use the game's original ordering tables for visibility.");
    changed |= DisplaySettings(&s->resample);
    return changed;
}

static bool SaturnSettings(SaturnRenderSettings *s)
{
    bool changed = Check("Saturn vertex snapping", &s->quantizeVertices,
        "Snaps compatibility geometry to whole console pixels. Native VDP1 draws snap to the selected render resolution.");
    changed |= Check("Affine texture mapping", &s->affineTextures,
        "Uses VDP1 quad drawing for native Saturn models and affine mapping for other assets. "
        "Native Saturn track tiles use their own palettes and perspective-correct mapping. "
        "Disable to use the compatibility geometry with perspective correction.");
    changed |= Check("RGB5 colour precision", &s->colourPrecision,
        "Uses 32 levels per colour channel without dithering. Native VDP1 pixels always use RGB555.");
    changed |= Check("Checkerboard transparency", &s->mesh,
        "Uses a console-pixel checkerboard for Saturn mesh effects, including exhaust flares and trails. "
        "Turn off for smooth transparency; additive light effects contribute half their intensity.");
    changed |= Check("Depth buffer (enhancement)", &s->depth,
        "Uses depth testing to reduce draw-order errors. Disabled by default for ordering-table visibility.");
    changed |= Check("Original Saturn sky scrolling", &s->originalSkyScroll,
        "Uses the original Saturn game's faster background scrolling on Saturn tracks. "
        "Disable to match the sky's motion to the world camera.");
    changed |= DisplaySettings(&s->resample);
    return changed;
}

static bool PcSettings(PcRenderSettings *s)
{
    bool changed = Check("Bilinear texture filtering", &s->bilinear,
        "Blends four neighbouring texels to smooth textures, softening their pixel edges. "
        "Disable for sharp, nearest-neighbour sampling.");
    changed |= Check("PC scenery and ship lighting", &s->lighting,
        "Uses PC-style scenery shading and applies the track's lighting colour to every ship vertex. "
        "Disable for PS1-style scenery and ship lighting.");
    changed |= Check("Depth buffer", &s->depth,
        "Uses polygon depth to help nearer surfaces hide farther ones, reducing draw-order errors. "
        "Disable to use ordering-table visibility.");
    changed |= DisplaySettings(&s->resample);
    return changed;
}

static bool N64Settings(N64RenderSettings *s)
{
    bool changed = Check("Three-point texture filtering", &s->threePoint,
        "Blends three neighbouring texels to reproduce N64-style texture smoothing. "
        "Disable for sharp, nearest-neighbour sampling. Does not filter the 2D HUD or menus.");
    changed |= Check("RGB5 framebuffer precision", &s->colourPrecision,
        "Reduces the rendered scene to 32 levels per red, green and blue channel before N64 display filtering. "
        "Also affects the race HUD. Disable for finer colour gradients.");
    ImGui::BeginDisabled(!s->colourPrecision);
    changed |= Check("Magic-square dithering", &s->dither,
        "Adds the N64 magic-square dither pattern when reducing colour precision, making gradients appear smoother. "
        "Also affects the race HUD. Requires RGB5 framebuffer precision.");
    changed |= Check("VI dedithering", &s->dedither,
        "Approximates the N64 Video Interface's recovery of finer colour shades from neighbouring pixels, "
        "reducing visible dithering in the scene and race HUD. Requires RGB5 framebuffer precision.");
    ImGui::EndDisabled();
    changed |= Check("VI divot filtering (approximate)", &s->divot,
        "Approximates the N64 Video Interface's removal of isolated pixel gaps by filtering high-contrast "
        "horizontal spikes. Also affects the race HUD and can soften small details.");
    changed |= DisplaySettings(&s->resample);
    return changed;
}

static bool ProfileSettings(RenderSettings *s, RendererId edit)
{
    bool changed = false;

    ImGui::PushID((int)edit);
    ImGui::SeparatorText(RenderGet(edit)->name);
    ImGui::PushTextWrapPos(0);
    switch (edit) {
    case RENDER_SATURN: changed |= SaturnSettings(&s->saturn); break;
    case RENDER_PC: changed |= PcSettings(&s->pc); break;
    case RENDER_N64: changed |= N64Settings(&s->n64); break;
    default: changed |= Ps1Settings(&s->ps1); break;
    }
    if (ImGui::Button("Restore renderer defaults")) {
        RenderDefaults(s, edit);
        changed = true;
    }
    Tooltip("Restores the default settings for this renderer profile only.");
    ImGui::PopTextWrapPos();
    ImGui::PopID();
    return changed;
}

bool RenderSettingsUi(RenderSettings *s)
{
    const char *names[] = {"PlayStation", "PC", "Sega Saturn"};
    RendererId base = s->selection == RENDER_PS1 || s->selection == RENDER_PC || s->selection == RENDER_SATURN
        ? s->selection : s->automaticBase;
    bool n64Tracks = s->selection == RENDER_AUTO || s->selection == RENDER_N64;
    int choice = base == RENDER_SATURN ? 2 : base == RENDER_PC;

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    bool changed = ImGui::Combo("Global renderer", &choice, names, 3);
    Tooltip("Selects PlayStation, PC or Saturn rendering. The N64 override below takes priority on Wipeout 64 "
        "tracks when enabled. Changes rendering only; the loaded game assets stay the same.");
    changed |= ImGui::Checkbox("Use N64 renderer for Wipeout 64 tracks", &n64Tracks);
    Tooltip("Automatically uses the N64 renderer on imported Wipeout 64 tracks. "
        "Disable to use the global renderer on those tracks too. Each renderer keeps its own settings.");
    if (changed) {
        base = choice == 2 ? RENDER_SATURN : choice == 1 ? RENDER_PC : RENDER_PS1;
        /* Retain the existing INI format: Automatic is the track-specific N64 override. */
        s->automaticBase = base;
        s->selection = n64Tracks ? RENDER_AUTO : base;
    }
    ImGui::Text("Active renderer: %s", RenderGet(RenderCurrent()->id)->name);

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
