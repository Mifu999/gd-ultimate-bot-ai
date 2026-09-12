#include <EffectObject.hpp>
#include <Player.hpp>

/// usedEffects is used to store information abuout objects that have already been collided with

bool EffectObject::touching(Player const& player) const {
	if (Object::touching(player)) {
		return !player.usedEffects.contains(id);
	}

	return false;
}

void EffectObject::collide(Player& player) const {
	player.usedEffects.insert(id);
}