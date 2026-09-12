#include "FrameClock.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace gdubai {

FrameClock& FrameClock::get() {
    static FrameClock instance;
    return instance;
}

void FrameClock::reset() {
    m_frame = 0;
}

double FrameClock::drift(PlayLayer* layer) const {
    if (!layer) return 0.0;
    double const gameFrame = layer->m_gameState.m_levelTime * kTPS;
    return static_cast<double>(m_frame) - gameFrame;
}

}  // namespace gdubai
