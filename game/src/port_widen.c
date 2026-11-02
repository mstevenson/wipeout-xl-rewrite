/* Port: widening of 32-bit file records to native records. See port_widen.h. */
#include "functions.h"
#include "port_widen.h"
#include <limits.h>
#include "../../platform/n64_material.h"

extern char errorString[];

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "the record conversion assumes a little-endian host"
#endif

void PortWidenRecords(const PortRecordTable *table, const void *source, int count, void *destination)
{
    const unsigned char *in = source;
    unsigned char *out = destination;
    unsigned f;
    unsigned slot;
    int i;

    for (i = 0; i < count; i++) {
        memset(out, 0, table->nativeSize);
        for (f = 0; f < table->fieldCount; f++) {
            const PortRecordField *field = &table->fields[f];
            if (!field->pointer) {
                memcpy(out + field->nativeOffset, in + field->fileOffset, field->fileSize);
            } else {
                for (slot = 0; slot < field->fileSize / 4; slot++)
                    memcpy(out + field->nativeOffset + slot * sizeof(void *), in + field->fileOffset + slot * 4, 4);
            }
        }
        in += table->fileSize;
        out += table->nativeSize;
    }
}

void PortNarrowRecords(const PortRecordTable *table, const void *source, int count, void *destination)
{
    const unsigned char *in = source;
    unsigned char *out = destination;
    unsigned f;
    unsigned slot;
    int i;

    for (i = 0; i < count; i++) {
        memset(out, 0, table->fileSize);
        for (f = 0; f < table->fieldCount; f++) {
            const PortRecordField *field = &table->fields[f];
            if (!field->pointer) {
                memcpy(out + field->fileOffset, in + field->nativeOffset, field->nativeSize);
            } else {
                for (slot = 0; slot < field->fileSize / 4; slot++)
                    memcpy(out + field->fileOffset + slot * 4, in + field->nativeOffset + slot * sizeof(void *), 4);
            }
        }
        in += table->nativeSize;
        out += table->fileSize;
    }
}

void *PortWidenHeapRecords(HeapBlock *heap, const PortRecordTable *table, char *source, int count)
{
    int bytes = count * (int)table->nativeSize;
    char *widened = DAlloc(heap, bytes + 32);

    if (!widened) {
        sprintf(errorString, "port_widen.c: Failed to allocate memory for %d %s records\n", count, table->name);
        Error(errorString, Fatal);
    }
    PortWidenRecords(table, source, count, widened);
    memset(widened + bytes, 0, 32);
    DFree(heap, source);
    return widened;
}

static int ReadBigShort(const unsigned char *p)
{
    return (short)((p[0] << 8) | p[1]);
}

/* The size of a primitive of the given type in the file (primitives have no pointers, so this is also
   the native size), or 0 for a type LoadPrmCommon does not know. */
static int PrimitiveSize(int type)
{
    switch (type) {
    case TypeF3: return sizeof(F3);
    case TypeFT3: return sizeof(FT3);
    case TypeF4: return sizeof(F4);
    case TypeFT4: return sizeof(FT4);
    case TypeG3: return sizeof(G3);
    case TypeGT3: return sizeof(GT3);
    case TypeG4: return sizeof(G4);
    case TypeGT4: return sizeof(GT4);
    case TypeTSPR:
    case TypeBSPR: return sizeof(SPR);
    case TypeLSF3: return sizeof(LSF3);
    case TypeLSFT3: return sizeof(LSFT3);
    case TypeLSF4: return sizeof(LSF4);
    case TypeLSFT4: return sizeof(LSFT4);
    case TypeLSG3: return sizeof(LSG3);
    case TypeLSGT3: return sizeof(LSGT3);
    case TypeLSG4: return sizeof(LSG4);
    case TypeLSGT4: return sizeof(LSGT4);
    case TypeSpline: return sizeof(Spline);
    case TypeInfiniteLight: return sizeof(InfiniteLight);
    case TypePointLight: return sizeof(PointLight);
    case TypeSpotLight: return sizeof(SpotLight);
    }
    return 0;
}

static const PortRecordField *FindField(const PortRecordTable *table, const char *name)
{
    unsigned f;

    for (f = 0; f < table->fieldCount; f++) {
        if (strcmp(table->fields[f].name, name) == 0)
            return &table->fields[f];
    }
    return 0;
}

static int AlignPrm(int offset)
{
    return (offset + PORT_PRM_ALIGNMENT - 1) & ~(PORT_PRM_ALIGNMENT - 1);
}

char *PortAlignPrm(char *start, char *p)
{
    return start + AlignPrm((int)(p - start));
}

int PortWidenPrm(const char *file, int length, char *destination)
{
    const unsigned char *in = (const unsigned char *)file;
    const PortRecordTable *objectTable = &PortRecord_Object;
    const PortRecordTable *skeletonTable = &PortRecord_Skeleton;
    const PortRecordField *vertexCountField = FindField(objectTable, "vertexCount");
    const PortRecordField *primitiveCountField = FindField(objectTable, "primitiveCount");
    int fixed = objectTable->fileSize + skeletonTable->fileSize;
    int pos = 0;
    int out = 0;

    if (destination)
        memset(destination, 0, PortWidenPrm(file, length, 0));
    while (length - pos >= fixed) {
        int vertexCount = ReadBigShort(in + pos + vertexCountField->fileOffset);
        int primitiveCount = ReadBigShort(in + pos + primitiveCountField->fileOffset);
        int end = pos + fixed;
        int i;
        int copied;

        if (vertexCount < 0 || primitiveCount < 0 || vertexCount > (length - end) / (int)sizeof(SVECTOR))
            break;
        end += vertexCount * sizeof(SVECTOR);
        for (i = 0; i < primitiveCount; i++) {
            int size = length - end >= 2 ? PrimitiveSize(ReadBigShort(in + end)) : 0;
            if (size == 0 || size > length - end)
                break;
            end += size;
        }
        if (i < primitiveCount)
            break;
        out = AlignPrm(out);
        if (destination) {
            PortWidenRecords(objectTable, in + pos, 1, destination + out);
            PortWidenRecords(skeletonTable, in + pos + objectTable->fileSize, 1, destination + out + objectTable->nativeSize);
        }
        out += objectTable->nativeSize + skeletonTable->nativeSize;
        copied = end - (pos + fixed);
        if (destination)
            memcpy(destination + out, in + pos + fixed, copied);
        out += copied;
        pos = end;
    }
    if (pos < length) {
        if (destination)
            memcpy(destination + out, in + pos, length - pos);
        out += length - pos;
    }
    return out;
}

/* The file offset of a primitive's texture number (0: untextured). */
static int TextureOffset(int type)
{
    switch (type) {
    case TypeFT3: return offsetof(FT3, texture);
    case TypeFT4: return offsetof(FT4, texture);
    case TypeGT3: return offsetof(GT3, texture);
    case TypeGT4: return offsetof(GT4, texture);
    case TypeLSFT3: return offsetof(LSFT3, texture);
    case TypeLSFT4: return offsetof(LSFT4, texture);
    case TypeLSGT3: return offsetof(LSGT3, texture);
    case TypeLSGT4: return offsetof(LSGT4, texture);
    case TypeTSPR:
    case TypeBSPR: return offsetof(SPR, texture);
    }
    return 0;
}

/* Rescale textured RGB from N64 to PS1 tint; preserve untextured colours and the fourth byte. */
static void NormalizeN64Primitive(unsigned char *p, int type)
{
    int offset = 0, count = 1;
    switch (type) {
    case TypeFT3: offset = offsetof(FT3, colour); break;
    case TypeFT4: offset = offsetof(FT4, colour); break;
    case TypeGT3: offset = offsetof(GT3, colour); count = 3; break;
    case TypeGT4: offset = offsetof(GT4, colour); count = 4; break;
    case TypeLSFT3: offset = offsetof(LSFT3, colour); break;
    case TypeLSFT4: offset = offsetof(LSFT4, colour); break;
    case TypeLSGT3: offset = offsetof(LSGT3, colour); count = 3; break;
    case TypeLSGT4: offset = offsetof(LSGT4, colour); count = 4; break;
    case TypeTSPR: case TypeBSPR: offset = offsetof(SPR, colour); break;
    default: return;
    }
    for (int i = 0; i < count; i++)
        for (int c = 0; c < 3; c++)
            p[offset + 4 * i + c] = PortN64Modulation(p[offset + 4 * i + c]);
}

static int ConvertPrm(char *file, int length, int shift, int n64)
{
    unsigned char *in = (unsigned char *)file;
    const PortRecordTable *objectTable = &PortRecord_Object;
    const PortRecordField *vertexCountField = FindField(objectTable, "vertexCount");
    const PortRecordField *primitiveCountField = FindField(objectTable, "primitiveCount");
    int fixed = objectTable->fileSize + PortRecord_Skeleton.fileSize;
    int pos = 0, i;

    while (length - pos >= fixed) {
        int vertexCount = ReadBigShort(in + pos + vertexCountField->fileOffset);
        int primitiveCount = ReadBigShort(in + pos + primitiveCountField->fileOffset);
        int end = pos + fixed;

        if (vertexCount < 0 || primitiveCount < 0 || vertexCount > (length - end) / (int)sizeof(SVECTOR))
            return 0;
        end += vertexCount * sizeof(SVECTOR);
        for (i = 0; i < primitiveCount; i++) {
            int type = length - end >= 2 ? ReadBigShort(in + end) : -1;
            int size = type >= 0 ? PrimitiveSize(type) : 0;
            int at = TextureOffset(type);

            if (size == 0 || size > length - end)
                return 0;
            if (at) {
                int texture = ReadBigShort(in + end + at) + shift;
                in[end + at] = (unsigned char)(texture >> 8);
                in[end + at + 1] = (unsigned char)texture;
            }
            if (n64) NormalizeN64Primitive(in + end, type);
            end += size;
        }
        pos = end;
    }
    return pos == length;
}

int PortPrmShiftTextures(char *file, int length, int shift)
{
    return ConvertPrm(file, length, shift, 0);
}

int PortPrmNormalizeN64(char *file, int length)
{
    return ConvertPrm(file, length, 0, 1);
}

/* Split unlit N64 quads into independently culled (0,1,2)/(2,1,3) triangles.
   These file records are pointer-free; see docs/N64_RENDERING.md for the ROM evidence. */
static void N64Triangle(const unsigned char *quad, int type, int half, unsigned char *triangle)
{
    static const int corners[2][3] = {{0, 1, 2}, {2, 1, 3}};
    int textured = type == TypeFT4 || type == TypeGT4;
    int gouraud = type == TypeG4 || type == TypeGT4;
    int fromColour = textured ? offsetof(FT4, colour) : offsetof(F4, colour);
    int toColour = textured ? offsetof(FT3, colour) : offsetof(F3, colour);
    int i;

    memset(triangle, 0, PrimitiveSize(type - 2));
    triangle[1] = (unsigned char)(type - 2);
    memcpy(triangle + offsetof(F3, flag), quad + offsetof(F4, flag), sizeof(short));
    if (textured) {
        memcpy(triangle + offsetof(FT3, texture), quad + offsetof(FT4, texture), 3 * sizeof(short));
        memcpy(triangle + offsetof(FT3, pad1), quad + offsetof(FT4, pad1), sizeof(short));
    }
    for (i = 0; i < 3; i++) {
        int corner = corners[half][i];
        memcpy(triangle + offsetof(F3, coords) + i * sizeof(short),
               quad + offsetof(F4, coords) + corner * sizeof(short), sizeof(short));
        if (textured)
            memcpy(triangle + offsetof(FT3, u0) + i * 2, quad + offsetof(FT4, u0) + corner * 2, 2);
        if (gouraud)
            memcpy(triangle + toColour + i * sizeof(CVECTOR), quad + fromColour + corner * sizeof(CVECTOR), sizeof(CVECTOR));
    }
    if (!gouraud)
        memcpy(triangle + toColour, quad + fromColour, sizeof(CVECTOR));
}

int PortPrmTriangulateN64(const char *file, int length, char *destination)
{
    const unsigned char *in = (const unsigned char *)file;
    const PortRecordTable *table = &PortRecord_Object;
    int verticesAt = FindField(table, "vertexCount")->fileOffset;
    int countAt = FindField(table, "primitiveCount")->fileOffset;
    int fixed = table->fileSize + PortRecord_Skeleton.fileSize;
    int pos = 0, out = 0;

    /* Validate/measure the entire stream before writing any output. */
    if (length < 0 || (destination && !PortPrmTriangulateN64(file, length, 0)))
        return 0;
    while (length - pos >= fixed) {
        int vertices = ReadBigShort(in + pos + verticesAt);
        int count = ReadBigShort(in + pos + countAt), newCount = count;
        int header = out, bytes, i;
        if (vertices < 0 || count < 0 || vertices > (length - pos - fixed) / (int)sizeof(SVECTOR))
            return 0;
        bytes = fixed + vertices * sizeof(SVECTOR);
        if (out > INT_MAX - bytes) return 0;
        if (destination) memcpy(destination + out, in + pos, bytes);
        pos += bytes;
        out += bytes;
        for (i = 0; i < count; i++) {
            int type = length - pos >= 2 ? ReadBigShort(in + pos) : 0;
            int size = PrimitiveSize(type), split = type == TypeF4 || type == TypeFT4 || type == TypeG4 || type == TypeGT4;
            int written = split ? 2 * PrimitiveSize(type - 2) : size;
            if (!size || size > length - pos || out > INT_MAX - written)
                return 0;
            if (split && ++newCount > SHRT_MAX) return 0;
            if (destination) {
                if (split) {
                    N64Triangle(in + pos, type, 0, (unsigned char *)destination + out);
                    N64Triangle(in + pos, type, 1, (unsigned char *)destination + out + written / 2);
                } else {
                    memcpy(destination + out, in + pos, size);
                }
            }
            pos += size;
            out += written;
        }
        if (destination) {
            destination[header + countAt] = (char)(newCount >> 8);
            destination[header + countAt + 1] = (char)newCount;
        }
    }
    return pos == length ? out : 0;
}

int portLastPrmImageLength;

char *PortWidenPrmImage(HeapBlock *heap, char *file, int *length)
{
    int widened = PortWidenPrm(file, *length, 0);
    char *image = DAlloc(heap, widened + 32);

    if (!image) {
        sprintf(errorString, "port_widen.c: Failed to allocate memory for the widened scene file\n");
        Error(errorString, Fatal);
    }
    PortWidenPrm(file, *length, image);
    memset(image + widened, 0, 32);
    DFree(heap, file);
    *length = widened;
    portLastPrmImageLength = widened;
    return image;
}
