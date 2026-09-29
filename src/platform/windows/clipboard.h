/**
 * @file src/platform/windows/clipboard.h
 * @brief Text-only Windows clipboard integration.
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <windows.h>

namespace platf::clipboard {
  /**
   * @brief Maximum accepted UTF-8 clipboard text size.
   */
  inline constexpr std::size_t MAX_TEXT_BYTES = 256 * 1024;

  /**
   * @brief Register a window to receive clipboard update notifications.
   */
  bool start(HWND window);

  /**
   * @brief Remove clipboard update notifications from a window.
   */
  void stop(HWND window);

  /**
   * @brief Read CF_UNICODETEXT and convert it to UTF-8.
   *
   * @return Text when available and valid, otherwise std::nullopt.
   */
  std::optional<std::string> read_text();

  /**
   * @brief Replace the Windows clipboard with UTF-8 text.
   *
   * @return true when the clipboard was updated.
   */
  bool write_text(std::string_view utf8_text);

  /**
   * @brief Process a WM_CLIPBOARDUPDATE notification.
   */
  void handle_update();
}  // namespace platf::clipboard
