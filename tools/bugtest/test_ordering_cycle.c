/* Real packet linking and table validation, including loops through empty OT entries. */
#include "../../platform/psx/libgpu.c"
#include <assert.h>
#include "../../platform/render/modern/metadata.c"
uint32_t GteFrameRecorded(void) { return 1; }
void GteKeepPast(int enabled) { (void)enabled; }
#ifndef main
PortSettings portSettings;
int PortLinkRole(void) { return 0; }
#endif
int PortTracing(void) { return 0; }

static void Check(u_long *head, u_long **expected, int count)
{
    int i;
    EndTableCycle(head);
    for (i = 0; i < count; i++) {
        assert(head == expected[i]);
        head = NodeAfter(head);
    }
    assert(!head);
}

int main(void)
{
    u_long ot[4], empty[4];
    POLY_F3 a = {0}, b = {0};
    DRAWENV env = {0};
    u_long *expected[] = {ot + 3, ot + 2, (u_long *)&b, (u_long *)&a};
    u_long *plain[] = {ot + 3, ot + 2, ot + 1, ot};

    ClearOTagR(ot, 4);
    Check(ot + 3, plain, 4);
    assert(!TableDraws(ot + 3));
    SetPolyF3(&a); SetPolyF3(&b);
    AddPrim(ot + 2, &a);
    AddPrim(ot + 2, &b);
    AddPrim(ot + 2, &a); /* duplicate insertion: a -> b -> a */
    expected[2] = (u_long *)&a; expected[3] = (u_long *)&b;
    Check(ot + 3, expected, 4);
    assert(a.tag >> 24 == 4 && b.tag >> 24 == 4);
    assert(TableDraws(ot + 3));

    /* An empty-entry cycle must be repaired before TableDraws/PacketFrom scans it. */
    ClearOTagR(empty, 4);
    empty[0] = LinkId(empty + 2);
    PsxUpcomingTables(empty + 3, NULL, NULL, &env);
    assert(!upcoming.table && NodeAfter(empty) == NULL);
    /* A loop in the second/HUD table is checked independently. */
    ClearOTagR(empty, 4);
    empty[0] = LinkId(empty);
    PsxUpcomingTables(ot + 3, empty + 3, NULL, &env);
    assert(upcoming.table == ot + 3 && !NodeAfter(empty));

    ClearOTagR(ot, 4);
    AddPrim(ot + 2, &a); /* the next frame still includes the complete, valid tail */
    u_long *next[] = {ot + 3, ot + 2, (u_long *)&a, ot + 1, ot};
    Check(ot + 3, next, 5);
    u_long saved[4]; memcpy(saved, ot, sizeof(ot));
    EndTableCycle(ot + 3);
    assert(!memcmp(saved, ot, sizeof(ot)));
    EndTableCycle(NULL);
    /* Vary both the noncyclic prefix and cycle period, including powers of two
       around Brent's checkpoints. Repair must cut only the repeated edge. */
    u_long chain[65];
    for (int length = 1; length <= 65; length++) {
        for (int entry = 0; entry < length; entry++) {
            for (int i = 0; i < length; i++)
                chain[i] = (4u << 24) | LinkId(&chain[i + 1 < length ? i + 1 : entry]);
            EndTableCycle(chain);
            for (int i = 0; i < length; i++) {
                assert(chain[i] >> 24 == 4);
                assert(NodeAfter(&chain[i]) == (i + 1 < length ? &chain[i + 1] : NULL));
            }
        }
    }
    /* Clearing a reused frame buffer expires only that buffer's HUD packet tags. */
    PortHudColourBars(&a, &b, ot, 0);
    PortHudColourBars(&a, &b, empty, 1);
    ClearOTagR(ot, 4);
    assert(!hudColourBars[0].fill[0] && !hudColourBars[0].fill[1]);
    assert(hudColourBars[1].fill[0] == (uint32_t *)&a + 1);
    ClearOTagR(empty, 4);
    assert(!hudColourBars[1].fill[0] && !hudColourBars[1].fill[1]);
    puts("cyclic scene/HUD/empty ordering tables end; packet payloads and valid frames preserved: OK");
    return 0;
}
