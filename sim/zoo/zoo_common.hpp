// =============================================================================
// sim/zoo/zoo_common.hpp — shared helpers for zoo bot TUs
//
// Provides:
//   * The Registry class (identical layout to campaign.cpp) so that zoo TUs
//     can register agents via REGISTER_AGENT without editing campaign.cpp.
//   * The REGISTER_AGENT macro (same semantics as in campaign.cpp).
//   * The payoff tensor (verbatim from the sample).
//   * Small utility lambdas for computing per-round payoffs and window
//     statistics used by the tier-1 bots.
//
// ODR note: the Registry class here is a pure inline definition (all
// members defined in-class, Meyers singleton for instance()). Since
// campaign.cpp defines the identical class in its own TU, the two inline
// definitions merge at link time and share the same function-local static
// map — no duplication, no ODR violation.
// =============================================================================

#ifndef MAS_SIM_ZOO_COMMON_HPP
#define MAS_SIM_ZOO_COMMON_HPP

#include "../engine.hpp"

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mas_sim {

// -----------------------------------------------------------------------------
// Registry — identical to the one in campaign.cpp (inline, Meyers singleton).
// -----------------------------------------------------------------------------
class Registry {
 public:
  using Factory = AgentFactory;

  static Registry& instance() {
    static Registry r;
    return r;
  }

  static bool register_factory(const std::string& name, Factory f) {
    instance().map_[name] = std::move(f);
    return true;
  }

  bool has(const std::string& name) const { return map_.count(name) != 0; }

  Factory get(const std::string& name) const {
    auto it = map_.find(name);
    if (it == map_.end()) {
      throw std::invalid_argument("unknown agent '" + name +
                                  "' (not registered in this build)");
    }
    return it->second;
  }

  size_t size() const { return map_.size(); }

  std::vector<std::string> names() const {
    std::vector<std::string> v;
    v.reserve(map_.size());
    for (const auto& kv : map_) v.push_back(kv.first);
    return v;
  }

 private:
  std::map<std::string, Factory> map_;
  Registry() = default;
  Registry(const Registry&) = delete;
  Registry& operator=(const Registry&) = delete;
};

// Link-time registration macro (same as in campaign.cpp).
#define REGISTER_AGENT(name)                                              \
  static const int mas_reg_agent_##name = []() {                         \
    ::mas_sim::Registry::register_factory(#name,                        \
                                          ::mas_sim::make_agent_##name()); \
    return 0;                                                            \
  }()

// -----------------------------------------------------------------------------
// Payoff tensor — verbatim from the sample.
// payoff[A][B][C]: A reads payoff[A][B][C], B reads payoff[B][C][A], etc.
// In the engine: agent at slot i sees opp1=slot(i+1)%3, opp2=slot(i+2)%3.
// Its payoff = kPayoff[myAction][opp1Action][opp2Action].
// -----------------------------------------------------------------------------
constexpr int kZooPayoff[2][2][2] = {
    {{4, 1}, {1, -1}},
    {{6, 3}, {3, 0}}
};

// Compute one player's payoff given three actions (this player, opp1, opp2).
inline int zoo_payoff(int me, int opp1, int opp2) {
  return kZooPayoff[me][opp1][opp2];
}

// -----------------------------------------------------------------------------
// RNG access: all stochastic zoo bots draw from fast_rng() (the sfc64
// thread-local handle). In kMsvcrt (fidelity) mode the same call site would
// need fidelity_rng(), but the zoo is designed for fast-mode tournaments
// (the campaign default). For a fidelity-mode port one would swap the call
// site; a single call-site per mode keeps the stream bit-exact (README).
// -----------------------------------------------------------------------------

// Draw a single bit (0=peace, 1=war) from the engine's fast RNG.
inline int zoo_draw_bit() {
  return fast_rng().rand() < 16384 ? 0 : 1;
}

// Draw a uniform [0,1) value from the engine's fast RNG.
inline double zoo_draw_uniform() {
  return fast_rng().uniform();
}

}  // namespace mas_sim

#endif  // MAS_SIM_ZOO_COMMON_HPP
