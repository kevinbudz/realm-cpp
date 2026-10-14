#include "realm/map/WorldFeedback.hpp"
#include "realm/objects/ConditionEffect.hpp"
#include <algorithm>
#include <utility>

namespace realm {
void WorldFeedback::clear() {
  damage_cues_.clear();
  status_events_.clear();
  status_head_ = 0;
}

void WorldFeedback::add_status(StatusEvent event) {
  if (status_events_.size() - status_head_ >= maximum_status_events) ++status_head_;
  status_events_.push_back(std::move(event));
  if (status_head_ >= maximum_status_events) {
    status_events_.erase(status_events_.begin(), status_events_.begin() + static_cast<std::ptrdiff_t>(status_head_));
    status_head_ = 0;
  }
}

void WorldFeedback::add_status(std::int32_t object_id, std::string text, std::uint32_t color, int lifetime,
                               int offset_time) {
  add_status({object_id, std::move(text), color, lifetime, offset_time});
}

std::vector<StatusEvent> WorldFeedback::take_status_events() {
  status_events_.erase(status_events_.begin(), status_events_.begin() + static_cast<std::ptrdiff_t>(status_head_));
  status_head_ = 0;
  return std::exchange(status_events_, {});
}

void WorldFeedback::report_damage(std::int32_t object_id, int amount, std::int32_t time,
                                  std::span<const std::uint8_t> effects, bool piercing, std::uint32_t condition_mask) {
  if (amount <= 0 && effects.empty()) return;

  // GameObject.damage: condition names stagger by 500 ms, then the damage amount.
  const bool pierced = piercing || (condition_mask & condition::bit(condition::armor_broken));
  int offset = 0;
  for (const auto effect : effects) {
    unsigned shown = effect;
    switch (effect) {
      case condition::quiet:
      case condition::weak:
      case condition::slowed:
      case condition::sick:
      case condition::dazed:
      case condition::blind:
      case condition::hallucinating:
      case condition::drunk:
      case condition::confused:
      case condition::stun_immune:
      case condition::invisible:
      case condition::paralyzed:
      case condition::speedy:
      case condition::bleeding:
      case condition::stasis:
      case condition::stasis_immune:
      case condition::armor_broken:
        break;
      case condition::stunned:
        if (condition_mask & condition::bit(condition::stun_immune)) {
          add_status(object_id, "Immune", 0xff0000U, 3000, 0);
          shown = condition::nothing;
        }
        break;
      default:
        shown = condition::nothing;
    }
    if (!shown) continue;
    add_status(object_id, std::string(condition_effect(shown).name), 0xff0000U, 3000, offset);
    offset += 500;
  }
  if (amount > 0) add_status(object_id, "-" + std::to_string(amount), pierced ? 0x9000ffU : 0xff0000U, 1000, 0);
  if (damage_cues_.size() >= maximum_damage_cues) damage_cues_.pop_front();
  damage_cues_.push_back({object_id, time, amount, pierced, {effects.begin(), effects.end()}});
}

void WorldFeedback::report_level_progress(std::int32_t object_id, int old_level, int old_exp, int old_fame, int level,
                                          int exp, int fame) {
  if (level > old_level)
    add_status(object_id, "Level Up!", 0x00ff00U, 2000, 0);
  else if (exp > old_exp) {
    if (level != 20) add_status(object_id, "+" + std::to_string(exp - old_exp) + " EXP", 0x00ff00U, 1000, 0);
    if (fame > old_fame && level == 20)
      add_status(object_id, "+" + std::to_string(fame - old_fame) + " Fame", 0xe25f00U, 1000, 0);
  }
}
}
