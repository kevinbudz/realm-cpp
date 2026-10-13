#pragma once
#include <vector>
#include <array>
struct SDL_Window;
struct SDL_Cursor;

namespace realm::platform {
// Construct before GPU resources; its destructor runs after their teardown.
class WindowSession {
 public:
  WindowSession() = default;
  ~WindowSession();
  WindowSession(const WindowSession&) = delete;
  WindowSession& operator=(const WindowSession&) = delete;
  void open(const char* title, int width, int height, bool hidden, int swap_interval);
  SDL_Window* window{};

 private:
  void* context{};
};
class CursorSet {
 public:
  CursorSet(std::vector<unsigned char> pixels, int width, int height);
  ~CursorSet();
  CursorSet(const CursorSet&) = delete;
  CursorSet& operator=(const CursorSet&) = delete;
  void select(int index);

 private:
  std::vector<unsigned char> pixels_;
  int width_{}, height_{};
  std::array<SDL_Cursor*, 16> cursors_{};
  int shown_{-2};
};
}
