#include "renderer_ui.h"
#include <imgui.h>
#include "../psx/gpu.h"
#include "../region.h"

static void Tooltip(const char *text, const char *heading = nullptr)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled) &&
        ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35);
        if (heading) {
            ImGui::TextUnformatted(heading);
            ImGui::Separator();
        }
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
    return changed;
}

static bool PcSettings(PcRenderSettings *s)
{
    bool changed = Check("Bilinear texture filtering", &s->bilinear,
        "Blends four neighbouring texels to smooth textures, softening their pixel edges. "
        "Disable for sharp, nearest-neighbour sampling.");
    changed |= Check("Dithered RGB565", &s->rgb565,
        "Applies ordered dithering and 16-bit colour to the finished picture before display scaling.");
    ImGui::TextUnformatted("PC draw distance");
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::SliderInt("##PC draw distance", &s->drawDistance, 1, PC_DRAW_DISTANCE_LEVELS,
                                "%d", ImGuiSliderFlags_NoInput);
    Tooltip("Selects the original PC road and scenery limits in Native mode. The default is level 1.");
    changed |= Check("PC scenery and ship lighting", &s->lighting,
        "Uses PC-style scenery shading and applies the track's lighting colour to every ship vertex. "
        "Disable for PS1-style scenery and ship lighting.");
    changed |= Check("Depth buffer", &s->depth,
        "Uses polygon depth to help nearer surfaces hide farther ones, reducing draw-order errors. "
        "Disable to use ordering-table visibility.");
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

static bool FrameRate(RenderSettings *s, RendererId id)
{
    RenderPacing *pacing = RenderPacingSetting(s, id);
    char original[32];
    snprintf(original, sizeof(original), "Original (%d fps)", RenderOriginalFrameRate(id, portPal));
    const char *const rates[] = {"Uncapped", "Display refresh (VSync)", "Capped", original};

    ImGui::TextUnformatted("Frame rate");
    ImGui::SetNextItemWidth(-1);
    bool changed = ImGui::Combo("##Frame rate", &pacing->rate, rates, IM_ARRAYSIZE(rates));
    Tooltip("Sets how often this profile presents a picture. The game simulation keeps its original timing.");
    if (pacing->rate == FRAME_RATE_CAPPED) {
        ImGui::TextUnformatted("Frames per second");
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::DragInt("##Frames per second", &pacing->cap, 1.0f, FRAME_CAP_MIN, FRAME_CAP_MAX,
                                  "%d", ImGuiSliderFlags_AlwaysClamp);
    }
    return changed;
}

struct LimitedDistanceText {
    char label[32];
    char limits[512];
};

static LimitedDistanceText LimitedDistance(const RenderSettings *settings, RendererId id)
{
    switch (id) {
    case RENDER_PC: {
        LimitedDistanceText text = {};
        RenderFrame frame = RenderSnapshot(id, settings);
        int sections, scenery;
        RenderPcViewLimits(&frame, &sections, &scenery);
        snprintf(text.label, sizeof(text.label), "Native (F3 level %d)", settings->pc.drawDistance);
        snprintf(text.limits, sizeof(text.limits),
            "Road caps each low-detail north run at %d sections. All-view lists use their authored lengths.\n"
            "Scenery uses depth bounds of 2048 to %d world units, accounting for object size.\n"
            "Ships use a shifted magnitude below 1200 (about 9600 world units), positive depth, "
            "and fewer than 40 sections of separation. The camera ship bypasses the magnitude and depth checks.",
            sections, scenery);
        return text;
    }
    case RENDER_N64:
        return {"Native (section budget)",
            "Road starts with a 22-section budget (20 on course 7). Heading, junctions, and course exceptions "
            "adjust it. Start-camera lists cap low-detail runs at 100 sections.\n"
            "Scenery uses depth bounds of 4096 to 76400 world units, accounting for object size.\n"
            "Ships are remembered within about 9600 world units. Rejected ships lose that history "
            "from about 23200 world units. A remembered ship in front can remain visible farther away."};
    case RENDER_SATURN:
        return {"Native (course depth)",
            "Road follows authored spans and tests the farthest tile corner against 40064 world units of depth "
            "(32000 on the two bonus courses).\n"
            "Scenery uses the same course depth limits with model bounds and polygon depth rules.\n"
            "Ships other than the camera ship need depth above 160 and below 32000 world units, "
            "plus a model bounds check."};
    case RENDER_MODERN:
        return {"Custom", "Sets how far the scene is visible from the camera. "
            "Distant scenery always fades into the sky over the final third of this distance."};
    default:
        return {"Native (regional limits)",
            "Road follows authored section lists. Directional low-detail runs cap at 100 sections.\n"
            "Scenery uses object bounds. PAL depths are 2048 to 38200 world units. NTSC near depth is "
            "2048 to 3072 (up to 4096 in link), and far depth is 37000 to 38200 (20000 to 38200 in link).\n"
            "Ships use a shifted magnitude below 1200 (about 9600 world units), positive depth, "
            "and fewer than 40 sections of separation. The camera ship bypasses the magnitude and depth checks."};
    }
}

static bool DrawDistance(const RenderSettings *settings, RendererId id, int *farView)
{
    const LimitedDistanceText limited = LimitedDistance(settings, id);
    const char *const distances[] = {limited.label, "Full (the whole track)"};
    ImGui::TextUnformatted("Draw distance");
    ImGui::SetNextItemWidth(-1);
    bool changed = ImGui::Combo("##Draw distance", farView, distances, IM_ARRAYSIZE(distances));
    Tooltip(limited.limits, limited.label);
    return changed;
}

static bool SceneEnhancements(RenderScene *scene, bool custom)
{
    int forcedFade = 1;
    ImGui::BeginDisabled(!scene->farView);
    bool changed = Check("Fade distant scenery", custom ? &forcedFade : &scene->fade,
        custom ? "Always enabled in Custom draw distance." :
                 "Fades distant scenery into the sky in Full draw distance.");
    changed |= Check("Engine flares on distant ships", &scene->farFlares,
        "Draws engine flares beyond the native distance in Full draw distance.");
    ImGui::EndDisabled();
    return changed;
}

static bool ProfileSettings(RenderSettings *s, RendererId edit)
{
    bool changed = false;
    RenderScene *scene = RenderSceneSetting(s, edit);

    ImGui::PushID((int)edit);
    ImGui::SeparatorText(RenderGet(edit)->name);
    ImGui::PushTextWrapPos(0);
    changed |= Resolution(s, edit);
    changed |= FrameRate(s, edit);
    changed |= DrawDistance(s, edit, &scene->farView);
    bool custom = edit == RENDER_MODERN && !scene->farView;
    if (custom) {
        ImGui::TextUnformatted("Custom distance");
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::SliderInt("##Custom distance", &s->modern.customDistance,
                                    MODERN_DISTANCE_MIN, MODERN_DISTANCE_MAX, "%d units", ImGuiSliderFlags_NoInput);
        Tooltip("The world fades fully into the sky at this distance from the camera.");
    }
    switch (edit) {
    case RENDER_MODERN: {
        const char *const detail[] = {"Native", "Medium", "High"};
        ImGui::TextUnformatted("Track Detail");
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::Combo("##Track Detail", &s->modern.trackDetail, detail, IM_ARRAYSIZE(detail));
        Tooltip("Native keeps the default detail for the selected draw distance. "
                "Medium raises Low tiles to Medium and Medium tiles to High. High uses High tiles throughout the visible road.");
        break;
    }
    case RENDER_SATURN: changed |= SaturnSettings(&s->saturn); break;
    case RENDER_PC: changed |= PcSettings(&s->pc); break;
    case RENDER_N64: changed |= N64Settings(&s->n64); break;
    default: changed |= Ps1Settings(&s->ps1); break;
    }

    ImGui::SeparatorText("Enhancements");
    changed |= SceneEnhancements(scene, custom);
    switch (edit) {
    case RENDER_MODERN: {
        changed |= Check("Bilinear texture filtering", &s->modern.bilinear,
            "Blends neighbouring texels on world textures. Disable for sharp, nearest-neighbour sampling. "
            "HUD and 2D menus use nearest sampling.");
        changed |= Check("Smooth track lighting", &s->modern.smoothTrackLighting,
            "Blends lighting across connected track surfaces. Ships pick up the tint beneath them. "
            "Special pads keep their own colours.");
        ImGui::TextUnformatted("Brightness");
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::SliderInt("##Brightness", &s->modern.brightness,
                                    MODERN_BRIGHTNESS_MIN, MODERN_BRIGHTNESS_MAX, "%d%%", ImGuiSliderFlags_NoInput);
        Tooltip("Controls world texture tint. 100% uses the PlayStation level. "
                "50% halves the tint and approximates the PC level.");
        changed |= DisplaySettings(&s->modern.resample);
        changed |= Check("Dynamic lights", &s->modern.weaponLights,
            "Projectiles, explosions, engines and active shields illuminate nearby surfaces. "
            "These lights do not cast shadows. Works with bloom off.");
        changed |= Check("Camera motion blur", &s->modern.cameraBlur,
            "Blurs scenery as the camera moves and ships as they move across the view. Keeps the HUD sharp.");
        ImGui::BeginDisabled(!s->modern.cameraBlur);
        changed |= ImGui::SliderInt("Blur amount", &s->modern.cameraBlurAmount, 0, MODERN_BLUR_MAX,
                                    "%d%%", ImGuiSliderFlags_NoInput);
        Tooltip("Controls how far moving scenery and ships streak across the screen.");
        ImGui::EndDisabled();
        const char *bloomTooltip = "Adds glow from overbright surfaces and track lights during races.";
        if (s->modernBloomOverride)
            bloomTooltip = RenderModernBloom(s)
                ? "WIPEOUT_MODERN_BLOOM enables bloom for this run. This checkbox sets the saved preference."
                : "WIPEOUT_MODERN_BLOOM disables bloom for this run. This checkbox sets the saved preference.";
        changed |= Check("HDR bloom", &s->modern.bloom, bloomTooltip);
        const char *tonemappers[] = {"Classic", "Soft Highlights"};
        ImGui::BeginDisabled(!RenderModernBloom(s) && !s->modern.weaponLights);
        ImGui::TextUnformatted("Tonemapper");
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::Combo("##Tonemapper", &s->modern.tonemapper, tonemappers, IM_ARRAYSIZE(tonemappers));
        Tooltip("Maps the HDR race picture to SDR when bloom or dynamic lights are enabled. "
                "Soft Highlights preserves Classic's shadows and rolls off bright colours earlier.");
        ImGui::EndDisabled();
        break;
    }
    case RENDER_SATURN:
        changed |= Check("Depth buffer", &s->saturn.depth,
            "Uses depth testing to reduce draw-order errors. Disabled by default for ordering-table visibility.");
        changed |= Check("Camera-aligned sky", &s->saturn.cameraAlignedSky,
            "Matches the sky's motion to the world camera on Saturn tracks. "
            "Disable for the original Saturn game's faster background scrolling.");
        changed |= DisplaySettings(&s->saturn.resample);
        break;
    case RENDER_PC:
        changed |= Check("Correct menu model aspect", &s->pc.correctMenuAspect,
            "Removes the original horizontal stretch from 3D models while keeping their height and placement. "
            "Disabled by default.");
        changed |= DisplaySettings(&s->pc.resample);
        break;
    case RENDER_N64:
        changed |= DisplaySettings(&s->n64.resample);
        break;
    default:
        changed |= Check("Depth buffer", &s->ps1.depth,
            "Uses polygon depth to help nearer surfaces hide farther ones, reducing draw-order errors. "
            "Disable to use the game's original ordering tables for visibility.");
        changed |= DisplaySettings(&s->ps1.resample);
        break;
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
    const RendererId profileOrder[] = {RENDER_MODERN, RENDER_PS1, RENDER_PC, RENDER_N64, RENDER_SATURN};
    const int profiles = IM_ARRAYSIZE(profileOrder);
    const char *names[profiles];
    for (int i = 0; i < profiles; i++) names[i] = RenderGet(profileOrder[i])->name;
    const struct {
        RendererId id;
        RendererId *selection;
    } platforms[] = {
        {RENDER_PS1, &s->ps1Selection}, {RENDER_PC, &s->pcSelection},
        {RENDER_N64, &s->n64Selection}, {RENDER_SATURN, &s->saturnSelection}
    };
    bool changed = Check("Use Modern for all platforms", &s->modernForAllPlatforms,
        "Uses the Modern profile for all discs and imported tracks. Turn this off to use the individual platform selections.");
    ImGui::Text("Active renderer: %s", RenderGet(RenderCurrent()->id)->name);
    if (s->selection != RENDER_AUTO)
        ImGui::TextWrapped("Environment override: %s", RenderGet(s->selection)->name);

    ImGui::BeginDisabled(s->modernForAllPlatforms);
    ImGuiTableFlags selectorFlags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable("Renderer platforms", IM_ARRAYSIZE(platforms), selectorFlags)) {
        for (int i = 0; i < IM_ARRAYSIZE(platforms); i++) {
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            ImGui::TextUnformatted(RenderGet(platforms[i].id)->name);
            RendererId &selection = *platforms[i].selection;
            int choice = 0;
            for (int j = 0; j < profiles; j++) {
                if (selection == profileOrder[j]) {
                    choice = j;
                    break;
                }
            }
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##Renderer", &choice, names, IM_ARRAYSIZE(names))) {
                selection = profileOrder[choice];
                changed = true;
            }
            Tooltip("Renderer profile used for this platform. Profiles keep their settings when switching platforms. "
                "Changing a profile's controls affects every platform using that profile.");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();

    /* Keep platform columns horizontal, with scrolling on smaller windows or larger fonts. */
    const ImGuiStyle &style = ImGui::GetStyle();
    float panelWidth = ImGui::CalcTextSize("Engine flares on distant ships").x +
        ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + style.CellPadding.x * 2;
    float width = panelWidth * profiles;
    if (width < ImGui::GetContentRegionAvail().x) width = ImGui::GetContentRegionAvail().x;
    float height = ImGui::GetFrameHeightWithSpacing() * (s->modern.scene.farView ? 24 : 26) + style.ScrollbarSize;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings |
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX;
    if (ImGui::BeginTable("Renderer settings", profiles, flags, ImVec2(0, height), width)) {
        for (RendererId id : profileOrder) {
            ImGui::TableNextColumn();
            changed |= ProfileSettings(s, id);
        }
        ImGui::EndTable();
    }
    return changed;
}
