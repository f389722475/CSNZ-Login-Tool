#pragma once
#include "game.hpp"
#include <optional>

namespace csnz {
struct CylinderContact {double fraction;Vec3 point;};
std::optional<CylinderContact> sweptCylinderContact(Vec3,Vec3,Vec3 lo,Vec3 hi,double radius,double height);
std::array<unsigned char,6> shieldGaugePayload(int entity,double remaining);
}
