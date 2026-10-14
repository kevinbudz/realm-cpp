#include "realm/map/Quest.hpp"

namespace realm {
void Quest::reset() {
  object_id_ = -1;
  available_at_ = 0;
  old_at_ = 0;
}
void Quest::set_object(std::int32_t object_id, std::int32_t time) {
  if (object_id_ == -1 && object_id != -1) {
    available_at_ = time + 4000;
    old_at_ = available_at_ + 2000;
  }
  object_id_ = object_id;
}
void Quest::completed(std::int32_t time) {
  available_at_ = static_cast<std::int32_t>(time + 15000 - unit_(random_) * 10000);
  old_at_ = available_at_ + 2000;
}
}
