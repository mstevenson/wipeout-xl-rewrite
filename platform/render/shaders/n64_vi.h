/* Private to n64_gl.c. N64 presentation is independent of other renderers.
   Quantize after scene blending, then optionally reconstruct colour bits.
   Final resampling uses GL's linear filter. See docs/N64_RENDERING.md. */
static const char *n64ViVertex =
    "void main() {\n"
    "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* The dedither algorithm follows paraLLEl-RDP's vi_fetch.frag (MIT; license below).
   Unlike a blur it changes only the low three colour bits, using a 3x3 neighbourhood. */
static const char *n64ViFragment =
    "#ifdef GL_ES\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "#endif\n"
    "uniform sampler2D source;\n"
    "uniform ivec4 region;\n" /* source x/y, output width/height */
    "uniform int stage;\n"
    "uniform int ditherOn;\n"
    "uniform int pixelScale;\n"
    "uniform ivec4 displayRegion;\n" /* destination origin and signed size for direct resampling */
    "out vec4 fragment;\n"
    "vec3 Sample(ivec2 p) {\n"
    "    return texelFetch(source, region.xy + clamp(p, ivec2(0), region.zw - 1), 0).rgb;\n"
    "}\n"
    "void main() {\n"
    "    if (stage == 2) {\n"
    "        vec2 at = vec2(region.xy) + (gl_FragCoord.xy - vec2(displayRegion.xy))\n"
    "            * vec2(region.zw) / vec2(displayRegion.zw);\n"
    "        at = clamp(at, vec2(region.xy) + 0.5, vec2(region.xy + region.zw) - 0.5);\n"
    "        fragment = vec4(texture(source, at / vec2(textureSize(source, 0))).rgb, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "    vec3 c = Sample(p);\n"
    "    if (stage == 0) {\n"
    "        const int magic[16] = int[16](0,6,1,7, 4,2,5,3, 3,5,2,4, 7,1,6,0);\n"
    "        ivec2 native = ((p + ivec2(region.x, 0)) / pixelScale) & 3;\n"
    "        vec3 rgb = floor(c * 255.0 + 0.5);\n"
    "        vec3 low = mod(rgb, 8.0);\n"
    "        vec3 up = ditherOn == 0 ? vec3(0.0) : step(vec3(float(magic[native.y * 4 + native.x]) + 0.5), low);\n"
    "        vec3 five = min(floor(rgb / 8.0) + up, 31.0);\n"
    "        c = (five * 8.0 + floor(five / 4.0)) / 255.0;\n"
    "    } else if (stage == 1) {\n"
    "        vec3 five = floor(c * 255.0 + 0.5) / 8.0;\n"
    "        five = floor(five);\n"
    "        vec3 adjustment = vec3(0.0);\n"
    "        for (int y = -1; y <= 1; y++) {\n"
    "            for (int x = -1; x <= 1; x++) {\n"
    "                vec3 other = floor(floor(Sample(p + ivec2(x,y) * pixelScale) * 255.0 + 0.5) / 8.0);\n"
    "                adjustment += clamp(other - five, -1.0, 1.0);\n"
    "            }\n"
    "        }\n"
    "        c = clamp((five * 8.0 + adjustment) / 255.0, 0.0, 1.0);\n"
    "    }\n"
    "    fragment = vec4(c, 1.0);\n" /* ROM disables gamma correction. */
    "}\n";

/* Copyright (c) 2020 Themaister
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
