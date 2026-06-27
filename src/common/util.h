/*
 * Copyright © 2026, Halide Compression, LLC.
 * All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef FMETRICS_UTIL_H
#define FMETRICS_UTIL_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <math.h>

static inline int imin(const int a, const int b) { return a < b ? a : b; }
static inline int imax(const int a, const int b) { return a > b ? a : b; }

static inline int iclip(const int v, const int min, const int max) {
    return v < min ? min : v > max ? max : v;
}

static inline double fclip(const double v, const double min, const double max) {
    return fmin(fmax(v, min), max);
}

static inline float fclipf(const float v, const float min, const float max) {
    return fmin(fmax(v, min), max);
}

static inline float flog2(const double v) {
    const union { const double d; const uint64_t x; } u = { v };
    const uint64_t mt = u.x & ((1ULL << 52) - 1);
    return (float)((u.x >> 52) & 0x7FF) - 1023.0 +
        (float)mt * (1.0 / (1ULL << 52));
}

static inline float flogf(const float v) {
    const union { const float d; const uint32_t x; } u = { v };
    const int e = ((u.x >> 23) & 0xff) - 127;
    const uint32_t m = u.x & 0x7fffff;
    const float f = m * (1.0f / 8388608.0f);
    return (e + f) * 0.69314718056f;
}

static const uint32_t TURBO_COLORMAP[256] = {
    0xff1b1723u, 0xff281a27u, 0xff341c2bu, 0xff3f1e2fu,
    0xff4a2033u, 0xff552336u, 0xff5f2539u, 0xff69283bu,
    0xff722a3eu, 0xff7b2c40u, 0xff842f42u, 0xff8c3144u,
    0xff943445u, 0xff9b3747u, 0xffa23948u, 0xffa93c49u,
    0xffaf3e49u, 0xffb5414au, 0xffbb444au, 0xffc1464bu,
    0xffc6494bu, 0xffcb4c4bu, 0xffcf4f4bu, 0xffd3514au,
    0xffd7544au, 0xffdb574au, 0xffdf5949u, 0xffe25c49u,
    0xffe55f48u, 0xffe86247u, 0xffea6546u, 0xffed6745u,
    0xffef6a44u, 0xfff06d43u, 0xfff27042u, 0xfff47241u,
    0xfff57540u, 0xfff6783fu, 0xfff77b3eu, 0xfff87d3du,
    0xfff8803bu, 0xfff9833au, 0xfff98639u, 0xfff98838u,
    0xfff98b37u, 0xfff98e35u, 0xfff89034u, 0xfff89333u,
    0xfff79632u, 0xfff69831u, 0xfff69b30u, 0xfff59e2fu,
    0xfff4a02eu, 0xfff2a32du, 0xfff1a52cu, 0xfff0a82bu,
    0xffeeaa2au, 0xffedad2au, 0xffebaf29u, 0xffeab228u,
    0xffe8b428u, 0xffe6b627u, 0xffe4b927u, 0xffe2bb26u,
    0xffe0bd26u, 0xffdec025u, 0xffdcc225u, 0xffdac425u,
    0xffd7c625u, 0xffd5c825u, 0xffd3ca25u, 0xffd1cd25u,
    0xffcecf25u, 0xffccd126u, 0xffc9d226u, 0xffc7d426u,
    0xffc4d627u, 0xffc2d827u, 0xffbfda28u, 0xffbddc29u,
    0xffbadd2au, 0xffb8df2bu, 0xffb5e12cu, 0xffb2e22du,
    0xffb0e42eu, 0xffade52fu, 0xffabe730u, 0xffa8e831u,
    0xffa6ea33u, 0xffa3eb34u, 0xffa0ec36u, 0xff9eee37u,
    0xff9bef39u, 0xff99f03bu, 0xff96f13du, 0xff94f23fu,
    0xff91f341u, 0xff8ff443u, 0xff8cf545u, 0xff8af647u,
    0xff87f749u, 0xff85f84bu, 0xff83f94eu, 0xff80f950u,
    0xff7efa52u, 0xff7cfa55u, 0xff79fb57u, 0xff77fb5au,
    0xff75fc5du, 0xff73fc5fu, 0xff71fd62u, 0xff6efd65u,
    0xff6cfd68u, 0xff6afd6au, 0xff68fe6du, 0xff66fe70u,
    0xff64fe73u, 0xff62fe76u, 0xff60fe79u, 0xff5efd7cu,
    0xff5dfd7fu, 0xff5bfd82u, 0xff59fd85u, 0xff57fc88u,
    0xff56fc8bu, 0xff54fc8eu, 0xff52fb91u, 0xff51fb95u,
    0xff4ffa98u, 0xff4ef99bu, 0xff4cf99eu, 0xff4bf8a1u,
    0xff49f7a4u, 0xff48f6a7u, 0xff46f6aau, 0xff45f5adu,
    0xff44f4b0u, 0xff42f3b3u, 0xff41f2b6u, 0xff40f0b9u,
    0xff3fefbcu, 0xff3eeebfu, 0xff3cedc2u, 0xff3bebc5u,
    0xff3aeac8u, 0xff39e9cbu, 0xff38e7cdu, 0xff37e6d0u,
    0xff36e4d3u, 0xff35e3d5u, 0xff34e1d8u, 0xff34dfdbu,
    0xff33deddu, 0xff32dcdfu, 0xff31dae2u, 0xff30d8e4u,
    0xff30d6e6u, 0xff2fd4e9u, 0xff2ed2ebu, 0xff2dd0edu,
    0xff2dceefu, 0xff2cccf1u, 0xff2bcaf3u, 0xff2bc8f4u,
    0xff2ac6f6u, 0xff2ac4f8u, 0xff29c1f9u, 0xff28bffbu,
    0xff28bdfcu, 0xff27bafdu, 0xff27b8ffu, 0xff26b5ffu,
    0xff26b3ffu, 0xff25b1ffu, 0xff25aeffu, 0xff24acffu,
    0xff24a9ffu, 0xff23a6ffu, 0xff23a4ffu, 0xff22a1ffu,
    0xff229fffu, 0xff229cffu, 0xff2199ffu, 0xff2197ffu,
    0xff2094ffu, 0xff2091ffu, 0xff1f8effu, 0xff1f8cffu,
    0xff1e89ffu, 0xff1e86ffu, 0xff1e83ffu, 0xff1d81ffu,
    0xff1d7effu, 0xff1c7bffu, 0xff1c78ffu, 0xff1b75ffu,
    0xff1b73ffu, 0xff1a70ffu, 0xff1a6dfeu, 0xff1a6afcu,
    0xff1968fbu, 0xff1965f9u, 0xff1862f8u, 0xff185ff6u,
    0xff175cf4u, 0xff175af3u, 0xff1657f1u, 0xff1654efu,
    0xff1552edu, 0xff144febu, 0xff144ce9u, 0xff134ae6u,
    0xff1347e4u, 0xff1245e2u, 0xff1242e0u, 0xff1140ddu,
    0xff103ddbu, 0xff103bd8u, 0xff0f38d6u, 0xff0f36d3u,
    0xff0e34d1u, 0xff0d31ceu, 0xff0d2fcbu, 0xff0c2dc9u,
    0xff0b2bc6u, 0xff0b29c4u, 0xff0a27c1u, 0xff0a25beu,
    0xff0923bcu, 0xff0821b9u, 0xff081fb7u, 0xff071db4u,
    0xff061cb1u, 0xff061aafu, 0xff0518acu, 0xff0417aau,
    0xff0416a8u, 0xff0314a5u, 0xff0213a3u, 0xff0212a1u,
    0xff01119fu, 0xff00109du, 0xff000f9bu, 0xff000e9au,
    0xff000e98u, 0xff000d96u, 0xff000c95u, 0xff000c94u,
    0xff000c93u, 0xff000c92u, 0xff000b91u, 0xff000c91u,
    0xff000c90u, 0xff000c90u, 0xff000c90u, 0xff000d90u,
};

#endif /* FMETRICS_UTIL_H */
