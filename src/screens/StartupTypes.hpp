#pragma once

namespace realm::startup_detail {
enum class Screen {
  account_loading,
  loading,
  title,
  characters,
  classes,
  skins,
  entering,
  game,
  fame,
  legends,
  editor
};
enum class Modal {
  none,
  login,
  reg,
  message,
  delete_confirm,
  deleting,
  delete_error,
  account_detail,
  change_password,
  slot_buying,
  slot_error
};
using Dialog = Modal;
enum class Task {
  none,
  verify_saved,
  char_list,
  login,
  reg,
  fame,
  legends,
  delete_character,
  change_password,
  buy_slot
};
enum Action : int {
  play_action = 1,
  account_action,
  legends_action,
  editor_action,
  login_action,
  register_action,
  back_action,
  classes_action,
  new_character_action,
  buy_slot_action,
  // Header hit areas below dialog_actions: an open dialog covers them.
  sound_action,
  rank_action,
  character_action = 100,
  class_action = 200,
  legend_tab_action = 300,
  legend_row_action = 400,
  delete_character_action = 700,
  news_action = 800,
  dialog_actions = 1000,
  cancel_action = dialog_actions,
  submit_action,
  link_action,
  account_logout_action,
  field_action = 1100
};
}
