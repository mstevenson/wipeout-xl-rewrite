/* Host-only, bounded packet annotations. Linking starts a new packet lifetime;
   the GTE frame epoch preserves annotations during interpolation redraws. */
#include "metadata.h"
#include "modern.h"
#include "../../psx/gpu.h"
#include "../../psx/gte.h"
#include "psyq.h"
#include <string.h>
#define SLOTS (1u << 16)
typedef struct PacketMetadata {
    const void *packet;
    uint32_t frame, colours[4];
    MaterialEmission emission;
    unsigned track : 1, spark : 1, ps1Light : 1;
} PacketMetadata;
static PacketMetadata entries[SLOTS];
static int populated;
static PacketMetadata *Find(const void *packet, int create)
{
    uintptr_t key = (uintptr_t)packet >> 2;
    key ^= key >> 17;
    unsigned slot = (unsigned)(key * 2654435761u) & (SLOTS - 1);
    uint32_t frame = GteFrameRecorded();
    for (unsigned i = 0; i < 32; i++) {
        PacketMetadata *entry = &entries[(slot + i) & (SLOTS - 1)];
        if (!entry->packet || entry->frame != frame) {
            if (!create) return NULL;
            *entry = (PacketMetadata){.packet = packet, .frame = frame};
            populated = 1;
            return entry;
        }
        if (entry->packet == packet) return entry;
    }
    return NULL; /* bounded failed enhancement, original material remains valid */
}
void ModernMetadataReset(void)
{
    if (populated) memset(entries, 0, sizeof(entries));
    populated = 0;
}
void ModernPacketReused(const void *packet)
{
    if (!populated) return;
    PacketMetadata *entry = Find(packet, 0);
    if (entry) { entry->track = entry->spark = entry->ps1Light = 0; entry->emission.enabled = 0; }
}
void PortTagTrackLight(const void *packet, const uint32_t colours[4])
{
    if (!ModernEnabled()) return;
    PacketMetadata *entry = Find(packet, 1);
    if (entry) { entry->track = 1; memcpy(entry->colours, colours, sizeof(entry->colours)); }
}
int ModernPacketTrack(const void *packet, uint32_t colours[4])
{
    if (!populated || !ModernEnabled()) return 0;
    PacketMetadata *entry = Find(packet, 0);
    if (!entry || !entry->track) return 0;
    memcpy(colours, entry->colours, sizeof(entry->colours));
    return 1;
}
void GpuTagEmission(const void *start, const void *end, const MaterialEmission *emission)
{
    if (!ModernEnabled()) return;
    const uint32_t *packet = start, *limit = end;
    while (packet < limit) {
        unsigned words = (*packet >> 24) + 1;
        if (words <= 1 || words > (size_t)(limit - packet)) break;
        PacketMetadata *entry = Find(packet, 1);
        if (entry) entry->emission = *emission;
        packet += words;
    }
}
int ModernPacketEmission(const void *packet, MaterialEmission *emission)
{
    if (!populated || !ModernEnabled()) return 0;
    PacketMetadata *entry = Find(packet, 0);
    if (!entry || !entry->emission.enabled) return 0;
    *emission = entry->emission;
    return 1;
}
void PortTagSparkStreak(const LINE_G2 *line)
{
    if (!ModernEnabled()) return;
    PacketMetadata *entry = Find(line, 1);
    if (entry) entry->spark = 1;
}
int ModernSparkPacket(const void *packet)
{
    if (!populated || !ModernEnabled()) return 0;
    PacketMetadata *entry = Find(packet, 0);
    return entry && entry->spark;
}

void PortTagTrackEncoding(const void *packet, int ps1)
{
    if (!ModernEnabled()) return;
    PacketMetadata *entry = Find(packet, 1);
    if (entry) entry->ps1Light = !!ps1;
}
int ModernPacketPs1Light(const void *packet)
{
    if (!populated || !ModernEnabled()) return 0;
    PacketMetadata *entry = Find(packet, 0);
    return entry && entry->ps1Light;
}
