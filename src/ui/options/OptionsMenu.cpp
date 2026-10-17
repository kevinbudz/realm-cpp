#include "realm/ui/options/OptionsMenu.hpp"
#include <algorithm>
#include <cctype>

namespace realm {
namespace {
char lower(char c) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool contains(std::string_view text, std::string_view word) {
  return std::search(text.begin(), text.end(), word.begin(), word.end(),
                     [](char a, char b) { return lower(a) == lower(b); }) != text.end();
}
}

bool option_matches(const OptionDef& option, std::string_view query) {
  const auto category = options_category_name(option.category);
  bool any = false;
  std::size_t at = 0;
  while (at < query.size()) {
    const auto end = std::min(query.find(' ', at), query.size());
    const auto word = query.substr(at, end - at);
    at = end + 1;
    if (word.empty()) {
      continue;
    }
    any = true;
    if (!contains(option.label, word) && !contains(option.section, word) &&
        !contains(category, word)) {
      return false;
    }
  }
  return any;
}

void OptionsMenu::open(Settings& settings) {
  settings_ = &settings;
  category_ = OptionsCategory::keybinds;
  query_.clear();
  capture_.clear();
  changed_ = home_ = false;
  refresh_rows();
  touched();
}

void OptionsMenu::close() {
  settings_ = nullptr;
  capture_.clear();
  query_.clear();
  rows_.clear();
  ++layout_revision_;
}

void OptionsMenu::set_category(OptionsCategory category) {
  if (!visible()) {
    return;
  }
  cancel_capture();
  if (category == category_ && query_.empty()) {
    return;
  }
  category_ = category;
  query_.clear();
  refresh_rows();
}

void OptionsMenu::set_query(std::string query) {
  if (!visible() || query == query_) {
    return;
  }
  cancel_capture();
  query_ = std::move(query);
  refresh_rows();
}

void OptionsMenu::refresh_rows() {
  rows_.clear();
  if (query_.find_first_not_of(' ') == std::string::npos) {
    rows_ = category_options(category_);
  } else {
    for (const auto& option : option_defs()) {
      if (option.kind != OptionDef::Kind::hidden_key &&
          option.kind != OptionDef::Kind::hidden_value && option_matches(option, query_)) {
        rows_.push_back(&option);
      }
    }
  }
  ++layout_revision_;
}

void OptionsMenu::touched() {
  ++value_revision_;
}

void OptionsMenu::begin_capture(std::string_view param) {
  const auto* option = find_option(param);
  if (!visible() || !option || option->kind != OptionDef::Kind::key || !enabled(*option)) {
    return;
  }
  capture_ = param;
  touched();
}

void OptionsMenu::cancel_capture() {
  if (!capture_.empty()) {
    capture_.clear();
    touched();
  }
}

void OptionsMenu::set_value(std::string_view param, int value) {
  const auto* option = find_option(param);
  if (!visible() || !option || !enabled(*option)) {
    return;
  }
  cancel_capture();
  if (settings_->set_value(param, value)) {
    changed_ = true;
    touched();
  }
}

void OptionsMenu::toggle(std::string_view param) {
  if (visible()) {
    set_value(param, settings_->flag(param) ? 0 : 1);
  }
}

void OptionsMenu::reset() {
  if (!visible()) {
    return;
  }
  cancel_capture();
  if (searching()) {
    for (const auto* option : rows_) {
      if (option->kind == OptionDef::Kind::key) {
        settings_->set_key(option->param, option->fallback);
      } else {
        settings_->set_value(option->param, option->fallback);
      }
    }
  } else {
    settings_->reset_category(category_);
  }
  changed_ = true;
  touched();
}

void OptionsMenu::request_close() {
  close();
}

void OptionsMenu::request_home() {
  close();
  home_ = true;
}

bool OptionsMenu::key_down(int code) {
  if (!visible()) {
    return false;
  }
  if (capturing()) {
    // KeyCodeBox.onInputKeyDown takes any key. Escape cancels here instead, because the
    // game binds it to leaving the game.
    if (code == key_escape) {
      cancel_capture();
    } else if (code != 0) {
      settings_->set_key(capture_, code);
      capture_.clear();
      changed_ = true;
      touched();
    }
    return true;
  }
  if (code == key_escape || (code != 0 && code == settings_->get("options"))) {
    close();
  }
  return true;
}
}
