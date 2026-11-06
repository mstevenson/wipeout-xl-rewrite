/* N64 tile addressing; source encoding remains independent of rasterizer selection. */
    "int N64Axis(int p, int low, int high, int mirror) {\n"
    "    int size = high - low + 1;\n"
    "    if (addressFlags == 0 || (size & (size - 1)) != 0) return clamp(p, low, high);\n"
    "    int at = p - low;\n"
    "    if ((addressFlags & mirror) != 0 && (at & size) != 0) at = ~at;\n"
    "    return low + (at & (size - 1));\n"
    "}\n"
