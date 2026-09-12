#include "CheckpointLedger.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace gdubai {

void CheckpointLedger::reset(AnchorState const& spawnState) {
    m_anchors.clear();
    Anchor spawn;
    spawn.checkpoint = nullptr;
    spawn.state = spawnState;
    spawn.frame = 0;
    spawn.macroLength = 0;
    m_anchors.push_back(spawn);
}

void CheckpointLedger::push(Anchor anchor) {
    m_anchors.push_back(std::move(anchor));
}

bool CheckpointLedger::pop() {
    if (m_anchors.size() <= 1) return false;
    m_anchors.pop_back();
    return true;
}

Anchor& CheckpointLedger::top() {
    if (m_anchors.empty()) reset(AnchorState{});
    return m_anchors.back();
}

Anchor const& CheckpointLedger::top() const {
    static Anchor const fallback{};
    if (m_anchors.empty()) return fallback;
    return m_anchors.back();
}

bool CheckpointLedger::verifyAgainst(PlayLayer* layer) const {
    if (!layer) return false;

    // m_checkpointArray holds only real checkpoints; our stack additionally
    // holds the spawn anchor, hence the +1.
    unsigned const gameCount =
        layer->m_checkpointArray ? layer->m_checkpointArray->count() : 0;
    unsigned const ledgerCount = static_cast<unsigned>(m_anchors.size());

    if (gameCount + 1 == ledgerCount) return true;

    log::error(
        "GDUBAI: checkpoint ledger desync - game has {} checkpoint(s), ledger has "
        "{} anchor(s) (expected {}). Aborting the run rather than committing to a "
        "wrong macro offset.",
        gameCount, ledgerCount, gameCount + 1);
    return false;
}

std::string CheckpointLedger::describeTop() const {
    if (m_anchors.empty()) return "no anchor";
    auto const& anchor = m_anchors.back();
    return fmt::format(
        "anchor {}/{} @ {} | macro {} ev | fails {}{}{}",
        m_anchors.size(), m_anchors.size(),
        anchor.state.describe(),
        anchor.macroLength,
        anchor.failures,
        anchor.simDistrusted ? " | sim distrusted" : "",
        anchor.fromMemory ? " | memory" : "");
}

}  // namespace gdubai
