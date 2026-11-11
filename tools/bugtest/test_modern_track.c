#include "../../game/src/portmodern.c"
#include <assert.h>
#include <stdio.h>
static TrackData data;
static HeapBlock courseMemory;
HeapBlock *heap = &courseMemory;
TrackData *track = &data;
static int smooth;
int PortOriginalTrackIs64(void) { return 0; }
RenderGeometry PortRenderGeometry(void) { return (RenderGeometry){.smoothTrackLighting = smooth}; }
static void ShipRoadLighting(void)
{
    VECTOR vertices[] = {
        {0,0,0}, {800,0,0}, {0,0,800}, {800,0,800}, {0,0,1600}, {800,0,1600},
        {1600,0,0}, {1600,0,800},
    };
    Face faces[] = {
        {.vertex = {1,0,2,3}, .normal = {.vy = 4096}, .flags = TRACK_BASE, .red = 32, .green = 64},
        {.vertex = {6,1,3,7}, .normal = {.vy = 4096}, .flags = TRACK_BASE, .red = 96, .green = 64},
        {.vertex = {3,2,4,5}, .normal = {.vy = 4096}, .flags = TRACK_BASE, .red = 224, .green = 128},
    };
    TrackSection sections[] = {{.faceCount = 2}, {.faceStart = 2, .faceCount = 1}};
    sections[0].nextSection.ptr = &sections[1];
    sections[1].prevSection.ptr = &sections[0];
    data = (TrackData){.faces = faces, .faceCount = 3, .vertices = vertices, .vertexCount = 8};
    ShipData ship = {.nearTrkSect = &sections[0], .attr = LSIDE, .ppivot = {400, -200, 400}};
    smooth = 0;
    assert(PortShipTrackLight(&ship).r == 32);
    ship.attr = 0;
    assert(PortShipTrackLight(&ship).r == 96);
    smooth = 1; PortModernTrackLoaded();

    /* Sample the same triangle field as the track, even when LSIDE disagrees. */
    uint32_t colours[4];
    assert(PortModernTrackCorners(&faces[0], colours));
    uint32_t expected = PortModernTrackSample(colours, 4, 4);
    CVECTOR light = PortShipTrackLight(&ship);
    assert(light.r == (expected & 255) && light.g == ((expected >> 8) & 255));
    ship.ppivot.vx = 800;
    CVECTOR laneEdge = PortShipTrackLight(&ship);
    ship.attr = LSIDE;
    assert(PortShipTrackLight(&ship).r == laneEdge.r);

    /* The nearest section may switch before or after the physical boundary. */
    ship.ppivot.vx = 400;
    ship.ppivot.vz = 800;
    light = PortShipTrackLight(&ship);
    ship.nearTrkSect = &sections[1];
    assert(PortShipTrackLight(&ship).r == light.r);
    ship.ppivot.vz = 799;
    assert(abs(PortShipTrackLight(&ship).r - light.r) <= 1);
    ship.ppivot.vz = 801;
    ship.nearTrkSect = &sections[0];
    assert(abs(PortShipTrackLight(&ship).r - light.r) <= 1);

    ship.ppivot.vz = 1200;
    CVECTOR before = PortShipTrackLight(&ship);
    sections[0].nextSection.ptr = NULL;
    sections[0].junction.ptr = &sections[1];
    assert(PortShipTrackLight(&ship).r == before.r);
    faces[2].red = 240; faces[2].green = 128; faces[2].blue = 0; PortModernTrackColour(&faces[2]);
    assert(PortShipTrackLight(&ship).r > before.r);
    faceEncoding[2] = RENDER_LIGHT_RGB8;
    light = PortShipTrackLight(&ship);
    assert(light.r == 240 && light.cd == RENDER_LIGHT_RGB8);

    /* A distant pivot falls back to its legacy section, away from the track. */
    ship.ppivot.vy = -2000;
    assert(PortShipTrackLight(&ship).r == 32);
    assert(faceEncoding && trackLightBuilt);
    PortModernHeapReset(NULL); assert(faceEncoding && trackLightBuilt);
    PortModernHeapReset(heap); assert(!faceEncoding && !trackLightBuilt && !courseHeap);
    puts("Ship road tint follows the track triangles across lane and section boundaries: OK");
}

int main(void)
{
    ShipRoadLighting();
    Face faces[] = {
        {.vertex={1,0,2,3}, .normal={0,4096,0}, .flags=TRACK_BASE, .red=32},
        {.vertex={3,2,4,5}, .normal={0,4096,0}, .flags=TRACK_BASE, .red=224},
        {.vertex={3,2,6,7}, .normal={0,4096,0}, .flags=TRACK_BASE|SPEED_UP, .red=255}
    };
    data=(TrackData){.faces=faces,.faceCount=2,.vertexCount=8};
    PortModernTrackLoaded(); smooth=1;
    uint32_t a[4], b[4];
    assert(PortModernTrackCorners(&faces[0],a));
    assert(PortModernTrackCorners(&faces[1],b));
    assert(a[2]==128 && a[3]==128 && b[0]==128 && b[1]==128);
    faces[1].red=128;
    assert(PortModernTrackCorners(&faces[0],a) && a[2]==80);
    data.faceCount=3; PortModernTrackLoaded();
    assert(!PortModernTrackCorners(&faces[0],a)); /* third, even excluded incident face is ambiguous */
    data.faceCount=2; faces[1].flags=0; PortModernTrackLoaded();
    assert(!PortModernTrackCorners(&faces[0],a)); /* wall/road separation */
    faces[1].flags=TRACK_BASE; faces[1].normal=(SVector){4096,0,0}; PortModernTrackLoaded();
    assert(!PortModernTrackCorners(&faces[0],a));
    assert(faceEncoding && trackLightBuilt);
    PortModernHeapReset(NULL); assert(faceEncoding && trackLightBuilt);
    PortModernHeapReset(heap); assert(!faceEncoding && !trackLightBuilt && !courseHeap);
    puts("Modern adjacency: ambiguity, markers, live RGB, creases and reloads: OK");
}
