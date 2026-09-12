#pragma once
#include <Object.hpp>

/**
 * Anything that subclasses EffectObject will be single-activate
 * unless otherwise specified. This matches how GD handles orbs,
 * pads, and portals.
 */
struct EffectObject : public Object {
    using Object::Object;
    bool touching(Player const&) const override;
    void collide(Player&) const override;
};
