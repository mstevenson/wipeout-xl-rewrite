/* Keyboard navigation through the production renderer selector and settings panels. */
#include "../../platform/render/renderer_ui.cpp"
#include "../../platform/settings.h"
#include "imgui_internal.h"
#include <cassert>
#include <cstdio>
PortSettings portSettings;
static int n64Track;
extern "C" int PortOriginalTrackIs64(void) { return n64Track; }
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
static void SelectRenderer(bool pc)
{
    focusSelector = true; Frame();
    Key(ImGuiKey_Enter); Key(pc ? ImGuiKey_DownArrow : ImGuiKey_UpArrow); Key(ImGuiKey_Enter);
    RendererId expected = pc ? RENDER_PC : RENDER_PS1;
    assert(portSettings.render.automaticBase == expected);
    assert(portSettings.render.selection == RENDER_AUTO || portSettings.render.selection == expected);
}
static void Layout(int columns)
{
    ImGuiWindow *w = ImGui::FindWindowByName("Graphics");
    ImGuiTable *table = ImGui::TableFindByID(ImHashStr("Renderer settings", 0, w->IDStack[0]));
    assert(table && table->ColumnsCount == columns);
    for (int i = 0; i < columns; i++) {
        assert(table->Columns[i].ContentMaxXUnfrozen <= table->Columns[i].MaxX);
    }
}
static ImGuiID WidgetId(const char *label, RendererId profile = RENDER_AUTO)
{
    ImGuiWindow *w = ImGui::FindWindowByName("Graphics");
    int id = profile;
    if (profile == RENDER_AUTO) return ImHashStr(label, 0, w->IDStack[0]);
    ImGuiID table = ImHashStr("Renderer settings", 0, w->IDStack[0]);
    ImGuiID scope = ImHashData(&id, sizeof(id), table);
    return ImHashStr(label, 0, scope);
}
static void Activate(const char *label, RendererId profile = RENDER_AUTO)
{
    ImGuiID target = WidgetId(label, profile);
    for (int i = 0; i < 32 && ImGui::GetCurrentContext()->NavId != target; i++) Key(ImGuiKey_Tab);
    assert(ImGui::GetCurrentContext()->NavId == target);
    Key(ImGuiKey_Space);
}
int main()
{
    portSettings.render = RENDER_DEFAULTS;
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr; io.ConfigNavCursorVisibleAlways = true; io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    unsigned char *pixels; int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    focusSelector = true; Frame();
    assert(portSettings.render.selection == RENDER_AUTO);
    Layout(2);
    Activate("PS1 vertex snapping", RENDER_PS1);
    assert(!portSettings.render.ps1.quantizeVertices && portSettings.render.ps1.dither);
    SelectRenderer(true);
    assert(RenderCurrent()->id == RENDER_PC);
    n64Track = 1; RenderCommit(); Frame();
    assert(RenderCurrent()->id == RENDER_N64);
    /* PC controls remain visible while an N64 track is using the override. */
    Activate("Bilinear texture filtering", RENDER_PC);
    assert(!portSettings.render.pc.bilinear && !portSettings.render.ps1.quantizeVertices);
    Activate("VI divot filtering (approximate)", RENDER_N64);
    assert(!portSettings.render.n64.divot && !portSettings.render.pc.bilinear);
    Activate("Restore renderer defaults", RENDER_PC);
    assert(portSettings.render.pc.bilinear && !portSettings.render.n64.divot && !portSettings.render.ps1.quantizeVertices);
    Activate("Linear display resampling", RENDER_N64);
    Key(ImGuiKey_Tab);
    assert(ImGui::GetCurrentContext()->NavId == WidgetId("Restore renderer defaults", RENDER_N64));
    /* No depth checkbox between the final N64 control and the reset button. */
    Key(ImGuiKey_Space);
    assert(portSettings.render.n64.divot && portSettings.render.n64.resample && !portSettings.render.ps1.quantizeVertices);
    Activate("Use N64 renderer for Wipeout 64 tracks");
    assert(portSettings.render.selection == RENDER_PC && RenderCurrent()->id == RENDER_PC);
    SelectRenderer(false);
    assert(RenderCurrent()->id == RENDER_PS1);
    for (int i = 0; i < 20; i++) {
        Key(ImGuiKey_Tab);
        assert(ImGui::GetCurrentContext()->NavId != WidgetId("Three-point texture filtering", RENDER_N64));
        assert(ImGui::GetCurrentContext()->NavId != WidgetId("Restore renderer defaults", RENDER_N64));
    }
    Activate("Use N64 renderer for Wipeout 64 tracks");
    assert(portSettings.render.selection == RENDER_AUTO && RenderCurrent()->id == RENDER_N64);
    Activate("PS1 vertex snapping", RENDER_PS1);
    assert(portSettings.render.ps1.quantizeVertices);
    n64Track = 0; RenderCommit(); Frame();
    assert(RenderCurrent()->id == RENDER_PS1);
    windowWidth = 360; Frame(); Layout(1);
    SelectRenderer(true);
    Activate("VI divot filtering (approximate)", RENDER_N64);
    assert(!portSettings.render.n64.divot && RenderCurrent()->id == RENDER_PC);
    Layout(1);
    windowWidth = 800; Frame(); Layout(2);
    /* Font scaling also triggers the stacked layout before controls are clipped. */
    io.FontGlobalScale = 2; Frame(); Layout(1);
    io.FontGlobalScale = 1; Frame(); Layout(2);
    focusSelector = true; Frame();
    Key(ImGuiKey_Enter); Key(ImGuiKey_DownArrow); Key(ImGuiKey_Enter);
    assert(portSettings.render.automaticBase == RENDER_SATURN && RenderCurrent()->id == RENDER_SATURN);
    Activate("Checkerboard transparency", RENDER_SATURN);
    assert(!RenderCurrent()->settings.saturn.mesh);
    Activate("Original Saturn sky scrolling", RENDER_SATURN);
    assert(RenderCurrent()->settings.saturn.originalSkyScroll);
    Activate("Restore renderer defaults", RENDER_SATURN);
    assert(portSettings.render.saturn.mesh && !RenderCurrent()->settings.saturn.originalSkyScroll);
    windowWidth = 360; Frame(); Layout(1);
    ImGui::DestroyContext();
    puts("Renderer UI: global selector, track-specific N64 toggle, independent panels/reset and responsive layout: OK");
}
