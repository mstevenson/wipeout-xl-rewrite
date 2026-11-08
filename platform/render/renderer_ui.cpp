#include "renderer_ui.h"
#include "imgui.h"
#include "../psx/gpu.h"
#include "../region.h"

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

static bool Resolution(RenderSettings *s, RendererId id)
{
    int width, height;
    RenderNativeSize(id, portPal, &width, &height);
    int maximum = GpuMaxScale(id), automatic = GpuWindowScale(id);
    int *setting = RenderScaleSetting(s, id);
    char labels[17][64];
    const char *items[17];
    snprintf(labels[0], sizeof(labels[0]), "Auto (%dx%d)", width * automatic, height * automatic);
    items[0] = labels[0];
    for (int i = 1; i <= maximum && i <= 16; i++) {
        snprintf(labels[i], sizeof(labels[i]), "%dx%d%s", width * i, height * i,
                 i == 1 ? (id == RENDER_PC ? " (default)" : " (native)") : "");
        items[i] = labels[i];
    }
    int choice = *setting > maximum ? maximum : *setting;
    ImGui::TextUnformatted("Resolution");
    ImGui::SetNextItemWidth(-1);
    bool changed = ImGui::Combo("##Resolution", &choice, items, maximum + 1);
    Tooltip("Rendering resolution for this profile. Auto follows the window. Widescreen adds columns "
        "to this 4:3 base; the displayed image keeps its aspect ratio.");
    if (changed) *setting = choice;
    return changed;
}

static bool ProfileSettings(RenderSettings *s, RendererId edit)
{
    bool changed = false;

    ImGui::PushID((int)edit);
    ImGui::SeparatorText(RenderGet(edit)->name);
    ImGui::PushTextWrapPos(0);
    changed |= Resolution(s, edit);
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
    const char *names[] = {"PlayStation", "PC", "Nintendo 64", "Sega Saturn"};
    RendererId *selections[] = {&s->ps1Selection, &s->pcSelection, &s->n64Selection, &s->saturnSelection};
    bool changed = false;

    ImGui::Text("Active renderer: %s", RenderGet(RenderCurrent()->id)->name);
    ImGui::TextWrapped("Each platform uses its selected renderer profile when loaded.");
    if (s->selection != RENDER_AUTO)
        ImGui::TextWrapped("Environment override: %s", RenderGet(s->selection)->name);

    ImGuiTableFlags selectorFlags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable("Renderer platforms", 4, selectorFlags)) {
        for (int i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            ImGui::TextUnformatted(names[i]);
            int choice = (int)*selections[i] - RENDER_PS1;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##Renderer", &choice, names, IM_ARRAYSIZE(names))) {
                *selections[i] = (RendererId)(choice + RENDER_PS1);
                changed = true;
            }
            Tooltip("Renderer profile used for this platform. Profiles keep their settings when switching platforms. "
                "Changing a profile's controls affects every platform using that profile.");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    /* Keep platform columns horizontal, with scrolling on smaller windows or larger fonts. */
    const ImGuiStyle &style = ImGui::GetStyle();
    float panelWidth = ImGui::CalcTextSize("VI divot filtering (approximate)").x +
        ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + style.CellPadding.x * 2;
    float width = panelWidth * 4;
    if (width < ImGui::GetContentRegionAvail().x) width = ImGui::GetContentRegionAvail().x;
    float height = ImGui::GetFrameHeightWithSpacing() * 13 + style.ScrollbarSize;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings |
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX;
    if (ImGui::BeginTable("Renderer settings", 4, flags, ImVec2(0, height), width)) {
        for (int i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            changed |= ProfileSettings(s, (RendererId)(i + RENDER_PS1));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    return changed;
}
