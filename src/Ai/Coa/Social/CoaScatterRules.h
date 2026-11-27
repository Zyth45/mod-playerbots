/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COASCATTERRULES_H
#define PLAYERBOTS_COASCATTERRULES_H

#include <cmath>
#include <cstddef>

namespace CoaScatterRules
{
struct Point
{
    float x;
    float y;
};

inline Point Destination(float centerX, float centerY, float orientation, float distance,
                         std::size_t index, std::size_t count, unsigned ring)
{
    float const angle = orientation + static_cast<float>(2.0 * M_PI * index / count);
    float const radius = distance + ring * (distance + 1.0f);
    return { centerX + std::cos(angle) * radius, centerY + std::sin(angle) * radius };
}
}  // namespace CoaScatterRules

#endif
