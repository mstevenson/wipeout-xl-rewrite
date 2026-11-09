/* Keyboard navigation through the production renderer selector and settings panels. */
#include "../../platform/render/renderer_ui.cpp"
#include "../../platform/settings.h"
#include "imgui_internal.h"
#include <cassert>
#include <cstdio>
#include <cstring>
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
static bool focusOverride;
static float windowWidth = 800;
static void Frame(int count = 3)
{
    while (count--) {
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(windowWidth, 900); io.DeltaTime = 1.0f / 60;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(ImVec2(windowWidth,900));
        ImGui::Begin("Graphics");
        if (focusOverride) { ImGui::SetKeyboardFocusHere(); focusOverride = false; }
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
static void FocusWidget(const char *label, int id, bool profile)
{
    ImGuiID table = Table(profile ? "Renderer settings" : "Renderer platforms")->ID;
    ImGuiID scope = ImHashData(&id, sizeof(id), table);
    ImGuiID target = ImHashStr(label, 0, scope);
    if (!profile && ImGui::GetCurrentContext()->NavWindow == Table()->InnerWindow)
        Key(ImGuiKey_Escape);
    for (int i = 0; i < 100 && ImGui::GetCurrentContext()->NavId != target; i++) {
        if (profile && ImGui::GetCurrentContext()->NavId == Table()->ID) Key(ImGuiKey_Enter);
        else Key(ImGuiKey_Tab);
    }
    if (ImGui::GetCurrentContext()->NavId != target)
        fprintf(stderr, "Missing %s for %s %d: wanted %x, nav %x, table %x, inner %s\n",
                label, profile ? "profile" : "platform", id, target, ImGui::GetCurrentContext()->NavId,
                Table()->ID, Table()->InnerWindow->Name);
    assert(ImGui::GetCurrentContext()->NavId == target);
}
static void Focus(const char *label, RendererId profile)
{
    FocusWidget(label, (int)profile, true);
}
static void Activate(const char *label, RendererId profile)
{
    Focus(label, profile);
    Key(ImGuiKey_Space);
}
static void SelectNext(int platform)
{
    FocusWidget("##Renderer", platform, false);
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
    focusOverride = true; Frame(); Layout();
    assert(portSettings.render.modernForAllPlatforms && RenderCurrent()->id == RENDER_MODERN);
    Key(ImGuiKey_Space);
    assert(!portSettings.render.modernForAllPlatforms);
    assert(RenderCurrent()->id == RENDER_PS1);
    Focus("##Resolution", RENDER_MODERN);
    ImGuiWindow *navWindow = ImGui::GetCurrentContext()->NavWindow;
    float modernX = navWindow->Pos.x + navWindow->NavRectRel[ImGuiNavLayer_Main].GetCenter().x;
    assert(modernX >= Table()->Columns[0].MinX && modernX <= Table()->Columns[0].MaxX);
    Focus("##Frame rate", RENDER_PS1);
    Key(ImGuiKey_Enter); Key(ImGuiKey_UpArrow); Key(ImGuiKey_UpArrow); Key(ImGuiKey_Enter);
    assert(PortFrameRate() == FRAME_RATE_VSYNC && portSettings.render.pc.pacing.rate == FRAME_RATE_CONSOLE);
    Key(ImGuiKey_Enter); Key(ImGuiKey_DownArrow); Key(ImGuiKey_Enter);
    assert(PortFrameRate() == FRAME_RATE_CAPPED && PortFrameCap(0) == FRAME_CAP_DEFAULT);
    Focus("##Frames per second", RENDER_PS1);
    io.AddInputCharactersUTF8("90"); Frame();
    Key(ImGuiKey_Enter);
    assert(PortFrameCap(0) == 90 && portSettings.render.pc.pacing.cap == FRAME_CAP_DEFAULT);
    Focus("##Draw distance", RENDER_PS1);
    Key(ImGuiKey_Enter); Key(ImGuiKey_DownArrow); Key(ImGuiKey_Enter);
    Activate("Fade distant scenery", RENDER_PS1);
    Activate("Engine flares on distant ships", RENDER_PS1);
    Focus("##Draw distance", RENDER_PS1);
    Key(ImGuiKey_Enter); Key(ImGuiKey_UpArrow); Key(ImGuiKey_Enter);
    const RenderScene *scene = RenderSceneFor(RenderCurrent());
    assert(!scene->farView && scene->fade && scene->farFlares);
    Activate("PS1 vertex snapping", RENDER_PS1);
    assert(!portSettings.render.ps1.quantizeVertices && portSettings.render.ps1.dither);
    Focus("##Resolution", RENDER_SATURN);
    Key(ImGuiKey_Enter); Key(ImGuiKey_DownArrow); Key(ImGuiKey_Enter);
    assert(portSettings.render.saturn.scale == 2 && portSettings.render.ps1.scale == 1);
    Activate("Restore renderer defaults", RENDER_SATURN);
    assert(portSettings.render.saturn.scale == 1);
    SelectNext(0);
    assert(portSettings.render.ps1Selection == RENDER_PC && RenderCurrent()->id == RENDER_PC);
    assert(PortFrameRate() == FRAME_RATE_CONSOLE);
    scene = RenderSceneFor(RenderCurrent());
    assert(!scene->farView && !scene->fade && !scene->farFlares);
    n64Track = 1; RenderCommit(); Frame();
    assert(RenderCurrent()->id == RENDER_N64);
    Activate("Bilinear texture filtering", RENDER_PC);
    assert(!portSettings.render.pc.bilinear && !portSettings.render.ps1.quantizeVertices);
    Activate("Dithered RGB565", RENDER_PC);
    assert(!portSettings.render.pc.rgb565);
    Activate("Correct menu model aspect", RENDER_PC);
    assert(portSettings.render.pc.correctMenuAspect && RenderCurrent()->id == RENDER_N64);
    const NativeDistanceText pcDefault = NativeDistance(&portSettings.render, RENDER_PC);
    assert(!strcmp(pcDefault.label, "Native (F3 level 1)"));
    assert(strstr(pcDefault.limits, "50 sections") && strstr(pcDefault.limits, "36000 world units"));
    Focus("##PC draw distance", RENDER_PC);
    Key(ImGuiKey_Space); Key(ImGuiKey_RightArrow); Key(ImGuiKey_Space);
    assert(portSettings.render.pc.drawDistance == 2);
    const NativeDistanceText pcAdjusted = NativeDistance(&portSettings.render, RENDER_PC);
    assert(!strcmp(pcAdjusted.label, "Native (F3 level 2)"));
    assert(strstr(pcAdjusted.limits, "45 sections") && strstr(pcAdjusted.limits, "29000 world units"));
    Activate("VI divot filtering (approximate)", RENDER_N64);
    assert(!portSettings.render.n64.divot);
    Activate("Restore renderer defaults", RENDER_PC);
    assert(portSettings.render.pc.bilinear && !portSettings.render.n64.divot);
    assert(portSettings.render.pc.rgb565);
    assert(portSettings.render.pc.drawDistance == 1);
    assert(!portSettings.render.pc.correctMenuAspect);
    SelectNext(2);
    assert(portSettings.render.n64Selection == RENDER_SATURN && RenderCurrent()->id == RENDER_SATURN);
    Activate("Checkerboard transparency", RENDER_SATURN);
    assert(!RenderCurrent()->settings.saturn.mesh);
    assert(!RenderCurrent()->settings.saturn.cameraAlignedSky);
    Activate("Camera-aligned sky", RENDER_SATURN);
    assert(RenderCurrent()->settings.saturn.cameraAlignedSky);
    Activate("Linear display resampling", RENDER_SATURN);
    assert(RenderCurrent()->settings.saturn.resample && !portSettings.render.ps1.resample);
    Activate("Restore renderer defaults", RENDER_SATURN);
    assert(portSettings.render.saturn.mesh && !RenderCurrent()->settings.saturn.cameraAlignedSky);
    assert(!portSettings.render.saturn.resample && !portSettings.render.ps1.quantizeVertices);
    FocusWidget("##Renderer", 2, false);
    Key(ImGuiKey_Enter); Key(ImGuiKey_Home); Key(ImGuiKey_Enter);
    assert(portSettings.render.n64Selection == RENDER_MODERN && RenderCurrent()->id == RENDER_MODERN);
    assert(RenderCurrent()->settings.modern.bilinear);
    Activate("Bilinear texture filtering", RENDER_MODERN);
    assert(!RenderCurrent()->settings.modern.bilinear);
    Focus("##Brightness", RENDER_MODERN);
    Key(ImGuiKey_Space); Key(ImGuiKey_LeftArrow); Key(ImGuiKey_Space);
    assert(RenderCurrent()->settings.modern.brightness < MODERN_BRIGHTNESS_DEFAULT);
    assert(RenderCurrent()->settings.modern.tonemapper == MODERN_TONEMAP_SOFT_SHADOWS);
    Focus("##Tonemapper", RENDER_MODERN);
    Key(ImGuiKey_Enter); Key(ImGuiKey_UpArrow); Key(ImGuiKey_Enter);
    assert(RenderCurrent()->settings.modern.tonemapper == MODERN_TONEMAP_ORIGINAL);
    assert(RenderCurrent()->settings.modern.weaponLights);
    Activate("Dynamic lights", RENDER_MODERN);
    assert(!RenderCurrent()->settings.modern.weaponLights);
    Activate("HDR bloom", RENDER_MODERN);
    assert(!RenderCurrent()->settings.modern.bloom);
    Activate("Engine flares on distant ships", RENDER_MODERN);
    assert(!RenderSceneFor(RenderCurrent())->farFlares);
    Activate("Fade distant scenery", RENDER_MODERN);
    assert(!RenderSceneFor(RenderCurrent())->fade);
    Activate("Restore renderer defaults", RENDER_MODERN);
    assert(RenderCurrent()->settings.modern.brightness == MODERN_BRIGHTNESS_DEFAULT);
    assert(RenderCurrent()->settings.modern.bilinear);
    assert(RenderCurrent()->settings.modern.bloom);
    assert(RenderCurrent()->settings.modern.weaponLights);
    assert(RenderCurrent()->settings.modern.tonemapper == MODERN_TONEMAP_SOFT_SHADOWS);
    assert(PortFrameRate() == FRAME_RATE_VSYNC);
    scene = RenderSceneFor(RenderCurrent());
    assert(scene->farView && scene->fade && scene->farFlares);
    n64Track = 0; RenderCommit(); Frame();
    assert(RenderCurrent()->id == RENDER_PC);
    windowWidth = 360; Frame(); Layout();
    Activate("Bilinear texture filtering", RENDER_PC);
    assert(!portSettings.render.pc.bilinear);
    io.FontGlobalScale = 2; Frame(); Layout();
    io.FontGlobalScale = 1; windowWidth = 1600; Frame(); Layout();
    ImGui::DestroyContext();
    puts("Renderer UI: horizontal platform selectors, shared profiles, independent routing and reset: OK");
}
