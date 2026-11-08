#include "n64_visibility.h"
#include <math.h>

static uint32_t IntegerRoot(uint64_t value)
{
    uint64_t bit = (uint64_t)1 << 62, result = 0;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= result + bit) { value -= result + bit; result = (result >> 1) + bit; }
        else result >>= 1;
        bit >>= 2;
    }
    return (uint32_t)result;
}

uint32_t N64NativeSquareRoot(uint32_t value)
{
    /* 0x80019f90: normalize, table estimate, then one integer Newton step.
       The ROM table is floor(sqrt(index << 24)); derive it without ROM data. */
    uint32_t normalized = value, shifts = 0, root;
    if (!value) return 0;
    while (!(normalized & 0xc0000000u)) { normalized <<= 2; shifts++; }
    root = IntegerRoot((uint64_t)(((normalized >> 23) + 1) >> 1) << 24);
    if (root > 65535) root = 65535;
    if (shifts) root = (root + (1u << (shifts - 1))) >> shifts;
    root = (root + value / root) >> 1;
    return root > 65535 ? 65535 : root;
}

uint32_t N64NativeMagnitude(int32_t dx, int32_t dy, int32_t dz)
{
    /* 0x800112a0 and 0x8001cec4. Imported world positions are unscaled;
       the original halves them only for rendering. Arithmetic shifts and
       wrapped 32-bit products are deliberate MIPS semantics. */
    uint32_t x = (uint32_t)(dx >> 3), y = (uint32_t)(dy >> 3), z = (uint32_t)(dz >> 3);
    return N64NativeSquareRoot(x * x + y * y + z * z);
}

int32_t N64NativeViewZ(const int16_t row[3], int32_t translation,
                       int32_t dx, int32_t dy, int32_t dz)
{
    /* 0x80014df4 shifts EACH product, unlike the PS1 GTE's dot product. */
    uint32_t result = (uint32_t)translation;
    result += (uint32_t)(((int16_t)dx * (int32_t)row[0]) >> 12);
    result += (uint32_t)(((int16_t)dy * (int32_t)row[1]) >> 12);
    result += (uint32_t)(((int16_t)dz * (int32_t)row[2]) >> 12);
    return (int32_t)result;
}

int N64NativeShipGate(N64NativeShipState *state, uint32_t magnitude, int32_t viewZ,
                     int cameraShip, int destroyed, int players)
{
    unsigned limit = players <= 1 ? 1200 : players == 2 ? 1000 : 450;
    int visible;
    /* 0x8001136c..113e8, 0x80011c64. A remembered ship in front remains
       visible even beyond 2900: that boundary clears only rejected ships. */
    if (magnitude < limit) state->remembered = 1;
    visible = !destroyed && state->remembered && (viewZ > 0 || cameraShip);
    if (!visible && magnitude >= 2900) state->remembered = 0;
    return visible;
}

int N64NativeShipVisible(N64NativeShipState *state, int32_t dx, int32_t dy, int32_t dz,
                        int32_t viewZ, int cameraShip, int destroyed, int players)
{
    return N64NativeShipGate(state, N64NativeMagnitude(dx, dy, dz), viewZ, cameraShip, destroyed, players);
}

int N64NativeSceneryVisible(int32_t viewZ, int32_t extent, int clipLevel)
{
    return N64NativeSceneryDepthVisible(viewZ >> 1, extent, clipLevel);
}

int N64NativeSceneryDepthVisible(int32_t renderDepth, int32_t extent, int clipLevel)
{
    /* 0x80029180..291b4; tables at 0x80091c44/64. The original halves
       PRM coordinates/extent; the import retains their full world units. */
    int near = clipLevel == 7 ? 3072 : 2048, far = clipLevel == 7 ? 37000 : 38200;
    int64_t z = renderDepth, radius = extent >> 1;
    return z + radius > near && z - radius < far;
}

void N64NativeCompose(const N64NativeMatrix *parent, const N64NativeMatrix *relative, N64NativeMatrix *out)
{
    N64NativeMatrix result;
    /* 0x80019ce0/0x800194f0: wrapped integer dot products, then shift. */
    for (int row = 0; row < 3; row++) {
        uint32_t sum = 0;
        for (int k = 0; k < 3; k++) sum += (uint32_t)relative->t[k] * (uint32_t)parent->m[row][k];
        result.t[row] = (int32_t)((uint32_t)parent->t[row] + (uint32_t)((int32_t)sum >> 12));
        for (int col = 0; col < 3; col++) {
            sum = 0;
            for (int k = 0; k < 3; k++) sum += (uint32_t)relative->m[k][col] * (uint32_t)parent->m[row][k];
            result.m[row][col] = (int16_t)((int32_t)sum >> 12);
        }
    }
    *out = result;
}

int N64NativeSceneryBoundsVisible(const int32_t position[3], int32_t extent, int clipLevel)
{
    /* 0x800291bc..292a4: both axes use 120, including the horizontal one. */
    int32_t radius = extent >> 1;
    int32_t far = (int32_t)((uint32_t)position[2] + (uint32_t)radius);
    int32_t x, y;
    if (!N64NativeSceneryDepthVisible(position[2], extent, clipLevel)) return 0;
    x = (int32_t)((uint32_t)position[0] * 160u) / far;
    y = (int32_t)((uint32_t)position[1] * 160u) / far;
    radius = (int32_t)((uint32_t)radius * 160u) / far;
    return x >= -120 - radius && x <= 120 + radius && y >= -120 - radius && y <= 120 + radius;
}

static int Valid(int index, int count) { return index >= 0 && index < count; }

int N64NativeObjectInRadius(int32_t dx, int32_t dy, int32_t dz, int32_t extent, int radius, int wide)
{
    int64_t x = dx, y = dy, z = dz, bound = (int64_t)radius * (wide ? 2 : 1);
    uint32_t square;
    if (x <= -bound || x >= bound || y <= -bound || y >= bound || z <= -bound || z >= bound) return 0;
    square = (uint32_t)dx * (uint32_t)dx + (uint32_t)dy * (uint32_t)dy + (uint32_t)dz * (uint32_t)dz;
    return (int64_t)N64NativeSquareRoot(square) - extent - 1024 < radius;
}

int N64NativeRoadWalk(const N64NativeSection *s, int size, int start, int players,
                     int count, int direction, int flag, N64NativeSectionEmit emit, void *context)
{
    int current = start, branch = -1, branchCount = 0, reverse = 0, branchPrevious = 0;
    int branchNext = 0, adjusted = 0, used = 0, list[128];
    if (!Valid(start, size)) return 0;
    /* 0x80016e8c..16f2c: extend across nearby junctions only on flag-zero runs. */
    if (!flag && (Valid(s[start].junction, size) ||
                  (Valid(s[start].previous, size) && Valid(s[s[start].previous].junction, size)) ||
                  (Valid(s[start].next, size) && Valid(s[s[start].next].junction, size)))) {
        for (int i = 0; i < 2 && Valid(current, size); i++)
            current = direction == 2 ? s[current].next : s[current].previous;
        count += players < 3 ? 2 : 1;
        adjusted = 1;
    }
    /* 0x80017004..17184: interleave at most ten branch sections. Both
       branches consume the common count; do not draw count on EACH branch. */
    if (count > 128) count = 128;
    while (used < count && Valid(current, size)) {
        list[used++] = current;
        if (!Valid(branch, size) && Valid(s[current].junction, size)) {
            branch = s[current].junction;
            if (!s[start].junctionFace) {
                branchPrevious = s[branch].next == current || s[branch].next == s[current].next;
                branchNext = s[branch].previous == current;
            } else {
                int neighbor = direction == 2 ? s[branch].previous : s[branch].next;
                reverse = Valid(neighbor, size) && !s[neighbor].junctionFace;
            }
        }
        if (Valid(branch, size) && branchCount < 10 && used < count) {
            list[used++] = branch;
            branchCount++;
            branch = branchPrevious || (!branchNext && direction == 2) ? s[branch].previous : s[branch].next;
        }
        current = (reverse ? direction == 2 : direction == 1) ? s[current].next : s[current].previous;
    }
    /* The original submits the assembled list backwards. */
    while (used) emit(list[--used], context);
    return adjusted;
}

static int Between(int value, int lo, int hi) { return value >= lo && value <= hi; }

int N64NativeRoadAngle(int heading, int32_t dx, int32_t dz)
{
    /* 0x80028a10 -> 0x80014c30 -> 0x80019350. This is not PS1 ratan2:
       it rounds the ratio to a 4096-entry atan table, then drops four bits.
       All 4097 table entries equal round(atan(i/4096)*32768/pi). */
    int x = (int16_t)dx, z = (int16_t)dz, angle, swapped = 0;
    unsigned a = z < 0 ? -z : z, b = x < 0 ? -x : x;
    if (!z) angle = !x ? 0 : x > 0 ? 16384 : -16384;
    else {
        unsigned ratio, remainder;
        if (a < b) { unsigned t = a; a = b; b = t; swapped = 1; }
        ratio = (b << 12) / a;
        remainder = (b << 12) % a;
        if (remainder * 2 > a) ratio++;
        angle = (int)(atan((double)ratio / 4096.0) * (32768.0 / 3.14159265358979323846) + 0.5);
        if (z < 0) {
            if (x < 0) angle = swapped ? -16384 - angle : -32768 + angle;
            else angle = swapped ? 16384 + angle : -32768 - angle;
        } else if (x < 0) angle = swapped ? angle - 16384 : -angle;
        else if (swapped) angle = 16384 - angle;
    }
    angle = (int16_t)angle >> 4;
    return (int)((unsigned)(heading - angle + 2048) & 4095) - 2048;
}

int N64NativeRoadSelect(const N64NativeSection *s, int size, int start, int players,
                       int course, int section, int angle, int reduced, int authored, int auxiliary,
                       N64NativeSectionEmit emit, void *context)
{
    int base = players <= 1 ? (course == 7 ? 20 : 22) : players == 2 ? 18 : players == 3 ? 14 : 10;
    int direction, adjusted = 0, count, special = 0;
    if (players >= 3 && reduced) base = 4;
    angle = (int)((unsigned)(angle + 2048) & 4095) - 2048;
    direction = angle >= -512 && angle < 512 ? 1 : angle < -1536 || angle >= 1536 ? 2 : 4;
    if (direction == 2 && authored && players < 3) return 1;
    if (direction == 1) {
        count = base - 2;
        if (players >= 3) count = base;
        else if (course == 5) {
            if (Between(section, 166, 190) || Between(section, 72, 91) ||
                Between(section, 266, 310) || Between(section, 390, 405)) { count = 15; special = 1; }
            else if (Between(section, 338, 350)) { count = 12; special = 1; }
        } else if (course == 4) {
            if (Between(section, 229, 250)) { count = 15; special = 1; }
            else if (Between(section, 251, 277)) { count = 18; special = 1; }
        } else if (course == 1 && (Between(section, 200, 253) || Between(section, 99, 134))) {
            count = 18; special = 1;
        }
        adjusted = N64NativeRoadWalk(s, size, start, players, count, 1, 0, emit, context);
        /* The >=3-player branch jumps directly to the call, then the auxiliary
           test: it does not execute the ordinary two-section reverse run. */
        if (players >= 3) adjusted = 0;
        else {
            if (special) adjusted = 0;
            if (!adjusted) N64NativeRoadWalk(s, size, start, players, 2, 2, 1, emit, context);
        }
        if (auxiliary && !adjusted) N64NativeRoadWalk(s, size, start, players, 2, 2, 1, emit, context);
    } else if (direction == 2) {
        adjusted = N64NativeRoadWalk(s, size, start, players, players == 4 ? base : base - 2, 2, 0, emit, context);
        if (players == 4) adjusted = 0;
        else if (!adjusted) N64NativeRoadWalk(s, size, start, players, 2, 1, 1, emit, context);
        if (auxiliary && !adjusted) N64NativeRoadWalk(s, size, start, players, 2, 1, 1, emit, context);
    } else {
        int backward = (int)((float)(angle >= 512 && angle < 1536 ? angle - 512 : -(angle + 512)) /
                             (float)(1000 / base));
        int forward = base - backward, minimum = players == 4 ? 2 : 8, flag;
        while (forward < minimum) { forward++; backward--; }
        while (backward < minimum) { backward++; forward--; }
        flag = backward < forward;
        N64NativeRoadWalk(s, size, start, players, forward, 1, flag, emit, context);
        N64NativeRoadWalk(s, size, start, players, backward, 2, flag, emit, context);
    }
    return 0;
}
