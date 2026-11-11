#include "modern.h"
#include "../../settings.h"
#include "../state.h"
#include "../../psx/gpu.h"
#include "imgui.h"

static bool Toggle(const char *label, int *value)
{
    bool enabled = *value != 0;
    if (!ImGui::Checkbox(label, &enabled)) return false;
    *value = enabled;
    return true;
}
int ModernUI(void)
{
    int changed = 0;
    bool enabled = portSettings.render.selection == RENDER_MODERN;
    if (ImGui::Checkbox("Modern renderer", &enabled)) {
        portSettings.render.selection = enabled ? RENDER_MODERN : RENDER_EXISTING;
        changed = 1;
    }
    if (!enabled) return changed;
    ModernRenderSettings &m = portSettings.render.modern;
    ImGui::PushID("modern");
    if (ImGui::CollapsingHeader("Modern settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= Toggle("Bilinear world textures", &m.bilinear);
        changed |= ImGui::SliderInt("World brightness", &m.brightness, MODERN_BRIGHTNESS_MIN, MODERN_BRIGHTNESS_MAX, "%d%%");
        changed |= Toggle("Smooth track lighting", &m.smoothTrackLighting);
        changed |= Toggle("Dynamic lights", &m.weaponLights);
        changed |= Toggle("HDR bloom", &m.bloom);
        const char *tones[] = {"Classic", "Soft Highlights"};
        changed |= ImGui::Combo("Tone mapper", &m.tonemapper, tones, 2);
        changed |= Toggle("Camera motion blur", &m.cameraBlur);
        if (m.cameraBlur) changed |= ImGui::SliderInt("Blur strength", &m.cameraBlurAmount, 0, MODERN_BLUR_MAX, "%d%%");
        ImGui::TextWrapped("Motion blur applies to solo races with valid camera history.");
        changed |= Toggle("Linear display resampling", &m.resample);
        const RenderDevice *device = RenderActiveDevice();
        if (device && !device->caps.hdrTargets && (m.bloom || m.cameraBlur))
            ImGui::TextWrapped("HDR is unavailable on this device. Bloom and motion blur are inactive; preferences are retained.");
        unsigned failed = GpuModernFailures();
        if (failed & 12) ImGui::TextWrapped("HDR resolve unavailable: using the sharp SDR picture.");
        if (failed & 1) ImGui::TextWrapped("Bloom allocation unavailable: glow is inactive.");
        if (failed & 2) ImGui::TextWrapped("Motion allocation unavailable: using a sharp picture.");
        if (ImGui::Button("Reset Modern settings")) { ModernReset(&portSettings.render); changed = 1; }
    }
    ImGui::PopID();
    return changed;
}
