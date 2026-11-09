/* Keyboard navigation through the production renderer selector and settings panels. */
#include "../../platform/render/renderer_ui.cpp"
#include "../../platform/settings.h"
#include "imgui_internal.h"
#include <cassert>
#include <cstdio>
PortSettings portSettings;
int portPal;
extern "C" int GpuMaxScale(RendererId id) { return id == RENDER_PC ? 6 : 12; }
extern "C" int GpuWindowScale(RendererId id) { return id == RENDER_PC ? 2 : 4; }
static int n64Track;
extern "C" int PortOriginalTrackIs64(void) { return n64Track; }
extern "C" int PortOriginalTrack(void) { return -1; }
extern "C" int PortPcData(void) { return 0; }
extern "C" int PortSaturnDisc(void) { return 0; }
extern "C" const struct Disc *PsxIoDisc(void) { return nullptr; }
static bool focusSelector;
static float windowWidth = 800;
static void Frame(int count = 3)
{
    while (count--) {
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(windowWidth, 900); io.DeltaTime = 1.0f / 60;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(ImVec2(windowWidth,900));
        ImGui::Begin("Graphics");
        if (focusSelector) { ImGui::SetKeyboardFocusHere(); focusSelector = false; }
        RenderSettingsUi(&portSettings.render);
        ImGui::End(); ImGui::Render(); RenderCommit();
    }
}
static void Key(ImGuiKey key)
{
    ImGui::GetIO().AddKeyEvent(key, true); Frame();
    ImGui::GetIO().AddKeyEvent(key, false); Frame();
}
static ImGuiTable *Table(const char *name = "Renderer settings")
{
    ImGuiWindow *w = ImGui::FindWindowByName("Graphics");
    return ImGui::TableFindByID(ImHashStr(name, 0, w->IDStack[0]));
}
static void Layout()
{
    ImGuiTable *table = Table();
    assert(table && table->ColumnsCount == RENDER_COUNT - RENDER_PS1);
    ImGuiTable *selectors = Table("Renderer platforms");
    assert(selectors && selectors->ColumnsCount == 4);
    for (int i = 0; i < 4; i++) assert(selectors->Columns[i].IsVisibleX);
    for (int i = 0; i < table->ColumnsCount; i++) {
        float minimum = ImGui::CalcTextSize("VI divot filtering (approximate)").x +
            ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
        assert(table->Columns[i].WidthGiven >= minimum);
        if (table->Columns[i].IsVisibleX)
            assert(table->Columns[i].ContentMaxXUnfrozen <= table->Columns[i].MaxX);
        if (i) assert(table->Columns[i].MinX >= table->Columns[i-1].MaxX);
    }
}
static ImGuiID WidgetId(const char *label, int platform, RendererId profile = RENDER_AUTO)
{
    ImGuiID table = Table(profile == RENDER_AUTO ? "Renderer platforms" : "Renderer settings")->ID;
    ImGuiID scope = ImHashData(&platform, sizeof(platform), table);
    if (profile != RENDER_AUTO) {
        int id = profile;
        scope = ImHashData(&id, sizeof(id), scope);
    }
    return ImHashStr(label, 0, scope);
}
static void Focus(const char *label, int platform, RendererId profile = RENDER_AUTO)
{
    ImGuiID target = WidgetId(label, platform, profile);
    if (profile == RENDER_AUTO && ImGui::GetCurrentContext()->NavWindow == Table()->InnerWindow)
        Key(ImGuiKey_Escape);
    for (int i = 0; i < 100 && ImGui::GetCurrentContext()->NavId != target; i++) {
        if (profile != RENDER_AUTO && ImGui::GetCurrentContext()->NavId == Table()->ID) Key(ImGuiKey_Enter);
        else Key(ImGuiKey_Tab);
    }
    if (ImGui::GetCurrentContext()->NavId != target)
        fprintf(stderr, "Missing %s platform %d profile %d: wanted %x, nav %x, table %x, inner %s\n",
                label, platform, profile, target, ImGui::GetCurrentContext()->NavId, Table()->ID, Table()->InnerWindow->Name);
    assert(ImGui::GetCurrentContext()->NavId == target);
}
static void Activate(const char *label, int platform, RendererId profile)
{
    Focus(label, platform, profile);
    Key(ImGuiKey_Space);
}
static void SelectNext(int platform)
{
    Focus("##Renderer", platform);
    Key(ImGuiKey_Enter); Key(ImGuiKey_DownArrow); Key(ImGuiKey_Enter);
}
int main()
{
    portSettings.render = RENDER_DEFAULTS;
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr; io.ConfigNavCursorVisibleAlways = true; io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    unsigned char *pixels; int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    focusSelector = true; Frame(); Layout();
    assert(RenderCurrent()->id == RENDER_PS1);
    Activate("PS1 vertex snapping", 0, RENDER_PS1);
    assert(!portSettings.render.ps1.quantizeVertices && portSettings.render.ps1.dither);
    Focus("##Resolution", 3, RENDER_SATURN);
    Key(ImGuiKey_Enter); Key(ImGuiKey_DownArrow); Key(ImGuiKey_Enter);
    assert(portSettings.render.saturn.scale == 2 && portSettings.render.ps1.scale == 1);
    Activate("Restore renderer defaults", 3, RENDER_SATURN);
    assert(portSettings.render.saturn.scale == 1);
    SelectNext(0);
    assert(portSettings.render.ps1Selection == RENDER_PC && RenderCurrent()->id == RENDER_PC);
    n64Track = 1; RenderCommit(); Frame();
    assert(RenderCurrent()->id == RENDER_N64);
    Activate("Bilinear texture filtering", 1, RENDER_PC);
    assert(!portSettings.render.pc.bilinear && !portSettings.render.ps1.quantizeVertices);
    Activate("Dithered RGB565", 1, RENDER_PC);
    assert(!portSettings.render.pc.rgb565);
    Activate("Correct menu model aspect", 1, RENDER_PC);
    assert(portSettings.render.pc.correctMenuAspect && RenderCurrent()->id == RENDER_N64);
    Focus("##PC draw distance", 1, RENDER_PC);
    Key(ImGuiKey_Space); Key(ImGuiKey_RightArrow); Key(ImGuiKey_Space);
    assert(portSettings.render.pc.drawDistance == 2);
    Activate("VI divot filtering (approximate)", 2, RENDER_N64);
    assert(!portSettings.render.n64.divot);
    Activate("Restore renderer defaults", 1, RENDER_PC);
    assert(portSettings.render.pc.bilinear && !portSettings.render.n64.divot);
    assert(portSettings.render.pc.rgb565);
    assert(portSettings.render.pc.drawDistance == 1);
    assert(!portSettings.render.pc.correctMenuAspect);
    SelectNext(2);
    assert(portSettings.render.n64Selection == RENDER_SATURN && RenderCurrent()->id == RENDER_SATURN);
    Activate("Checkerboard transparency", 3, RENDER_SATURN);
    assert(!RenderCurrent()->settings.saturn.mesh);
    assert(!RenderCurrent()->settings.saturn.cameraAlignedSky);
    Activate("Camera-aligned sky (enhancement)", 3, RENDER_SATURN);
    assert(RenderCurrent()->settings.saturn.cameraAlignedSky);
    Activate("Linear display resampling", 3, RENDER_SATURN);
    assert(RenderCurrent()->settings.saturn.resample && !portSettings.render.ps1.resample);
    Activate("Restore renderer defaults", 3, RENDER_SATURN);
    assert(portSettings.render.saturn.mesh && !RenderCurrent()->settings.saturn.cameraAlignedSky);
    assert(!portSettings.render.saturn.resample && !portSettings.render.ps1.quantizeVertices);
    SelectNext(2);
    assert(portSettings.render.n64Selection == RENDER_MODERN && RenderCurrent()->id == RENDER_MODERN);
    assert(RenderCurrent()->settings.modern.bilinear);
    Activate("Bilinear texture filtering", 4, RENDER_MODERN);
    assert(!RenderCurrent()->settings.modern.bilinear);
    Focus("##Brightness", 4, RENDER_MODERN);
    Key(ImGuiKey_Space); Key(ImGuiKey_LeftArrow); Key(ImGuiKey_Space);
    assert(RenderCurrent()->settings.modern.brightness < MODERN_BRIGHTNESS_DEFAULT);
    assert(RenderCurrent()->settings.modern.tonemapper == MODERN_TONEMAP_SOFT_SHADOWS);
    Focus("##Tonemapper", 4, RENDER_MODERN);
    Key(ImGuiKey_Enter); Key(ImGuiKey_UpArrow); Key(ImGuiKey_Enter);
    assert(RenderCurrent()->settings.modern.tonemapper == MODERN_TONEMAP_ORIGINAL);
    assert(RenderCurrent()->settings.modern.weaponLights);
    Activate("Dynamic lights", 4, RENDER_MODERN);
    assert(!RenderCurrent()->settings.modern.weaponLights);
    Activate("HDR bloom", 4, RENDER_MODERN);
    assert(!RenderCurrent()->settings.modern.bloom);
    Activate("Restore renderer defaults", 4, RENDER_MODERN);
    assert(RenderCurrent()->settings.modern.brightness == MODERN_BRIGHTNESS_DEFAULT);
    assert(RenderCurrent()->settings.modern.bilinear);
    assert(RenderCurrent()->settings.modern.bloom);
    assert(RenderCurrent()->settings.modern.weaponLights);
    assert(RenderCurrent()->settings.modern.tonemapper == MODERN_TONEMAP_SOFT_SHADOWS);
    n64Track = 0; RenderCommit(); Frame();
    assert(RenderCurrent()->id == RENDER_PC);
    windowWidth = 360; Frame(); Layout();
    Activate("Bilinear texture filtering", 1, RENDER_PC);
    assert(!portSettings.render.pc.bilinear);
    io.FontGlobalScale = 2; Frame(); Layout();
    io.FontGlobalScale = 1; windowWidth = 1600; Frame(); Layout();
    ImGui::DestroyContext();
    puts("Renderer UI: horizontal platform selectors, shared profiles, independent routing and reset: OK");
}
