#pragma once
#include <Object.hpp>

struct Hazard : public Object {
    Hazard(Vec2D size, std::unordered_map<int, std::string>&& fields);
    void collide(Player&) const override;
};

struct Sawblade : public Hazard {
    using Hazard::Hazard;
    bool touching(Player const&) const override;
};
